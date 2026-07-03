#include "SniHost.hh"

#include "wlr.hpp"   // wl_event_loop/-source (the only sanctioned wayland include)

#include <systemd/sd-bus.h>

#include <cstdio>
#include <cstring>

namespace bbai::sni {

  namespace {
    constexpr const char *kWatcherName = "org.kde.StatusNotifierWatcher";
    constexpr const char *kWatcherPath  = "/StatusNotifierWatcher";
    constexpr const char *kWatcherIface = "org.kde.StatusNotifierWatcher";
    constexpr const char *kItemIface    = "org.kde.StatusNotifierItem";

    // a(iiay) -> IconFrame list. Degenerate/mis-sized frames are dropped here,
    // once, instead of being every renderer's problem.
    void readFrames(sd_bus_message *m, std::vector<IconFrame> &out) {
      if (sd_bus_message_enter_container(m, 'a', "(iiay)") < 0) return;
      while (sd_bus_message_at_end(m, 0) == 0) {
        if (sd_bus_message_enter_container(m, 'r', "iiay") < 0) break;
        int32_t w = 0, h = 0;
        const void *bytes = nullptr;
        size_t len = 0;
        if (sd_bus_message_read(m, "ii", &w, &h) >= 0 &&
            sd_bus_message_read_array(m, 'y', &bytes, &len) >= 0 &&
            w > 0 && h > 0 && bytes &&
            len == 4u * unsigned(w) * unsigned(h)) {
          IconFrame f;
          f.width = w;
          f.height = h;
          f.data.assign(static_cast<const uint8_t *>(bytes),
                        static_cast<const uint8_t *>(bytes) + len);
          out.push_back(std::move(f));
        }
        sd_bus_message_exit_container(m);
      }
      sd_bus_message_exit_container(m);
    }
  }

  struct Host::Reg {
    Host *host = nullptr;
    std::string service, path, owner;
    sd_bus_slot *getall_slot = nullptr;   // in-flight GetAll (nullptr = none)
    sd_bus_track *track = nullptr;        // fires when `service` leaves the bus
    ~Reg() {
      if (getall_slot) sd_bus_slot_unref(getall_slot);
      if (track) sd_bus_track_unref(track);
    }
  };

  // sd-bus C callback glue. Static members of a private nested struct: nested
  // classes see Host's privates, and the vtable's designated-initializer
  // macros (C++20-clean, POC-proven at -Wpedantic on gcc 15/16) can name the
  // handlers from the static-member definition below.
  struct Host::Cb {
    static const sd_bus_vtable watcher_vtable[];
    static int registerItem(sd_bus_message *m, void *userdata, sd_bus_error *);
    static int registerHost(sd_bus_message *m, void *userdata, sd_bus_error *);
    static int getItems(sd_bus *, const char *, const char *, const char *,
                        sd_bus_message *reply, void *userdata, sd_bus_error *);
    static int getHostRegistered(sd_bus *, const char *, const char *, const char *,
                                 sd_bus_message *reply, void *userdata, sd_bus_error *);
    static int getVersion(sd_bus *, const char *, const char *, const char *,
                          sd_bus_message *reply, void *userdata, sd_bus_error *);
    static int onGetAll(sd_bus_message *reply, void *userdata, sd_bus_error *);
    static int onItemSignal(sd_bus_message *m, void *userdata, sd_bus_error *);
    static int onTrack(sd_bus_track *, void *userdata);
  };

  const sd_bus_vtable Host::Cb::watcher_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("RegisterStatusNotifierItem", "s", "", registerItem,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RegisterStatusNotifierHost", "s", "", registerHost,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("RegisteredStatusNotifierItems", "as", getItems, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("IsStatusNotifierHostRegistered", "b", getHostRegistered, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("ProtocolVersion", "i", getVersion, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_SIGNAL("StatusNotifierItemRegistered", "s", 0),
    SD_BUS_SIGNAL("StatusNotifierItemUnregistered", "s", 0),
    SD_BUS_SIGNAL("StatusNotifierHostRegistered", "", 0),
    SD_BUS_SIGNAL("StatusNotifierHostUnregistered", "", 0),
    SD_BUS_VTABLE_END
  };

  int Host::Cb::registerItem(sd_bus_message *m, void *userdata, sd_bus_error *) {
    auto *host = static_cast<Host *>(userdata);
    const char *arg = nullptr;
    if (sd_bus_message_read(m, "s", &arg) < 0 || !arg)
      return sd_bus_reply_method_return(m, "");
    const char *sender = sd_bus_message_get_sender(m);
    Registration reg = resolveRegistration(arg, sender ? sender : "");
    host->addRegistration(reg.service, reg.path, sender ? sender : "");
    return sd_bus_reply_method_return(m, "");
  }

  int Host::Cb::registerHost(sd_bus_message *m, void *userdata, sd_bus_error *) {
    auto *host = static_cast<Host *>(userdata);
    host->host_registered_ = true;
    sd_bus_emit_signal(host->bus_, kWatcherPath, kWatcherIface,
                       "StatusNotifierHostRegistered", "");
    sd_bus_emit_properties_changed(host->bus_, kWatcherPath, kWatcherIface,
                                   "IsStatusNotifierHostRegistered", nullptr);
    return sd_bus_reply_method_return(m, "");
  }

  int Host::Cb::getItems(sd_bus *, const char *, const char *, const char *,
                         sd_bus_message *reply, void *userdata, sd_bus_error *) {
    auto *host = static_cast<Host *>(userdata);
    int r = sd_bus_message_open_container(reply, 'a', "s");
    if (r < 0) return r;
    for (const auto &reg : host->regs_) {
      r = sd_bus_message_append(reply, "s", (reg->service + reg->path).c_str());
      if (r < 0) return r;
    }
    return sd_bus_message_close_container(reply);
  }

  int Host::Cb::getHostRegistered(sd_bus *, const char *, const char *, const char *,
                                  sd_bus_message *reply, void *userdata, sd_bus_error *) {
    auto *host = static_cast<Host *>(userdata);
    return sd_bus_message_append(reply, "b", host->host_registered_ ? 1 : 0);
  }

  int Host::Cb::getVersion(sd_bus *, const char *, const char *, const char *,
                           sd_bus_message *reply, void *, sd_bus_error *) {
    return sd_bus_message_append(reply, "i", 0);
  }

  int Host::Cb::onGetAll(sd_bus_message *reply, void *userdata, sd_bus_error *) {
    auto *reg = static_cast<Host::Reg *>(userdata);
    Host *host = reg->host;
    if (sd_bus_message_is_method_error(reply, nullptr)) {
      // The item never answered its properties: drop the registration so
      // items_ only ever holds materialized entries. Copy the keys out first -
      // dropRegistration destroys reg, and passing reg's own strings by
      // reference would leave them dangling mid-call. (Unref-from-own-callback
      // is safe: sd-bus holds a ref on the slot during dispatch.)
      const std::string service = reg->service, path = reg->path;
      host->dropRegistration(service, path);
      return 0;
    }

    Item item;
    item.service = reg->service;
    item.path = reg->path;

    if (sd_bus_message_enter_container(reply, 'a', "{sv}") < 0) return 0;
    while (sd_bus_message_at_end(reply, 0) == 0) {
      if (sd_bus_message_enter_container(reply, 'e', "sv") < 0) break;
      const char *key = nullptr;
      if (sd_bus_message_read(reply, "s", &key) < 0 || !key) break;
      char type = 0;
      const char *contents = nullptr;
      if (sd_bus_message_peek_type(reply, &type, &contents) < 0 || !contents) break;
      if (sd_bus_message_enter_container(reply, 'v', contents) < 0) break;

      const std::string k = key;
      if (k == "IconPixmap" && strcmp(contents, "a(iiay)") == 0) {
        readFrames(reply, item.icon_pixmaps);
      } else if (k == "ToolTip" && strcmp(contents, "(sa(iiay)ss)") == 0) {
        if (sd_bus_message_enter_container(reply, 'r', "sa(iiay)ss") >= 0) {
          const char *ticon = nullptr, *ttitle = nullptr, *tbody = nullptr;
          sd_bus_message_read(reply, "s", &ticon);
          sd_bus_message_skip(reply, "a(iiay)");
          if (sd_bus_message_read(reply, "ss", &ttitle, &tbody) >= 0 && ttitle)
            item.tooltip = ttitle;
          sd_bus_message_exit_container(reply);
        }
      } else if (k == "Menu" && strcmp(contents, "o") == 0) {
        const char *v = nullptr;
        sd_bus_message_read(reply, "o", &v);
        if (v) item.menu_path = v;
      } else if (k == "ItemIsMenu" && strcmp(contents, "b") == 0) {
        int v = 0;
        sd_bus_message_read(reply, "b", &v);
        item.item_is_menu = (v != 0);
      } else if (strcmp(contents, "s") == 0) {
        const char *v = nullptr;
        sd_bus_message_read(reply, "s", &v);
        if (v) {
          if (k == "Id") item.id = v;
          else if (k == "Title") item.title = v;
          else if (k == "Status") item.status = v;
          else if (k == "IconName") item.icon_name = v;
          else if (k == "IconThemePath") item.icon_theme_path = v;
          // other string props (Category, ...): read and dropped
        }
      } else {
        sd_bus_message_skip(reply, contents);   // AttentionIcon*, Overlay*, ...
      }

      sd_bus_message_exit_container(reply);     // v
      sd_bus_message_exit_container(reply);     // e
    }
    sd_bus_message_exit_container(reply);       // a

    host->storeItem(std::move(item));
    return 0;
  }

  int Host::Cb::onTrack(sd_bus_track *, void *userdata) {
    auto *reg = static_cast<Host::Reg *>(userdata);
    // The tracked name is gone: the item's connection died. Copy the keys out
    // first - dropRegistration destroys reg (unref-from-own-callback is safe,
    // sd-bus refs the track during dispatch).
    Host *host = reg->host;
    const std::string service = reg->service, path = reg->path;
    host->dropRegistration(service, path);
    return 0;
  }

  int Host::Cb::onItemSignal(sd_bus_message *m, void *userdata, sd_bus_error *) {
    auto *host = static_cast<Host *>(userdata);
    const char *sender = sd_bus_message_get_sender(m);
    const char *path = sd_bus_message_get_path(m);
    if (!sender || !path) return 0;
    for (auto &reg : host->regs_)
      if (reg->path == path &&
          (reg->owner == sender || reg->service == sender)) {
        host->fetchAll(*reg);          // storeItem's upsert fires itemChanged
        break;
      }
    return 0;
  }

  Host::Host(wl_event_loop *loop) : loop_(loop) {
    sd_bus *bus = nullptr;
    if (sd_bus_open_user(&bus) < 0) {
      // No session bus (bare TTY login, headless CI): tray off, compositor fine.
      return;
    }
    if (sd_bus_add_object_vtable(bus, &watcher_slot_, kWatcherPath, kWatcherIface,
                                 Cb::watcher_vtable, this) < 0) {
      sd_bus_flush_close_unref(bus);
      return;
    }
    // Claim the tray. flags 0 = don't queue, don't steal: if another tray owns
    // the watcher name we go inert rather than fight it.
    int r = sd_bus_request_name(bus, kWatcherName, 0);
    if (r < 0) {
      fprintf(stderr, "blackboxai: SNI watcher name taken (%d) - tray disabled\n", r);
      sd_bus_slot_unref(watcher_slot_);
      watcher_slot_ = nullptr;
      sd_bus_flush_close_unref(bus);
      return;
    }
    bus_ = bus;

    // Item change signals, one wildcard match each (any sender/path with the
    // item interface); onItemSignal resolves whose they are. A match failing
    // just means no live updates for that signal - not worth dying over.
    static const char *const kChangeSignals[] = { "NewIcon", "NewTitle",
                                                  "NewStatus", "NewToolTip" };
    for (const char *sig : kChangeSignals)
      sd_bus_match_signal(bus_, nullptr, nullptr, nullptr, kItemIface, sig,
                          Cb::onItemSignal, this);
  }

  Host::~Host() { teardownBus(); }

  void Host::teardownBus() {
    regs_.clear();   // Reg holds sd-bus resources later; they die before the bus
    if (fd_source_)    { wl_event_source_remove(fd_source_);    fd_source_ = nullptr; }
    if (timer_source_) { wl_event_source_remove(timer_source_); timer_source_ = nullptr; }
    if (watcher_slot_) { sd_bus_slot_unref(watcher_slot_);      watcher_slot_ = nullptr; }
    if (bus_) { sd_bus_flush_close_unref(bus_); bus_ = nullptr; }  // releases our names
  }

  void Host::drain() {
    if (!bus_) return;
    for (;;) {
      int r = sd_bus_process(bus_, nullptr);
      if (r < 0) {          // connection died (bus daemon gone): go inert
        teardownBus();
        return;
      }
      if (r == 0) break;
    }
  }

  void Host::processForTest() { drain(); }

  void Host::addRegistration(const std::string &service, const std::string &path,
                             const std::string &owner) {
    for (auto &r : regs_)
      if (r->service == service && r->path == path) {
        fetchAll(*r);                   // re-registration = the app restarted its state
        return;
      }
    auto reg = std::make_unique<Reg>();
    reg->host = this;
    reg->service = service;
    reg->path = path;
    reg->owner = owner;
    regs_.push_back(std::move(reg));
    sd_bus_emit_signal(bus_, kWatcherPath, kWatcherIface,
                       "StatusNotifierItemRegistered", "s",
                       (service + path).c_str());
    sd_bus_emit_properties_changed(bus_, kWatcherPath, kWatcherIface,
                                   "RegisteredStatusNotifierItems", nullptr);
    Reg *r = regs_.back().get();
    if (sd_bus_track_new(bus_, &r->track, Cb::onTrack, r) >= 0)
      sd_bus_track_add_name(r->track, service.c_str());
    fetchAll(*regs_.back());
  }

  void Host::fetchAll(Reg &reg) {
    if (reg.getall_slot) {
      sd_bus_slot_unref(reg.getall_slot);   // supersede an in-flight fetch
      reg.getall_slot = nullptr;
    }
    sd_bus_call_method_async(bus_, &reg.getall_slot, reg.service.c_str(),
                             reg.path.c_str(),
                             "org.freedesktop.DBus.Properties", "GetAll",
                             Cb::onGetAll, &reg, "s", kItemIface);
  }

  void Host::storeItem(Item item) {
    for (Item &it : items_)
      if (it.service == item.service && it.path == item.path) {
        it = std::move(item);
        if (events_.itemChanged) events_.itemChanged(Item(it));   // copy: cb may re-enter
        return;
      }
    items_.push_back(std::move(item));
    if (events_.itemAdded) events_.itemAdded(Item(items_.back()));
  }

  void Host::dropRegistration(const std::string &service, const std::string &path) {
    for (auto it = regs_.begin(); it != regs_.end(); ++it)
      if ((*it)->service == service && (*it)->path == path) {
        regs_.erase(it);                  // Reg dtor unrefs its slots
        break;
      }
    sd_bus_emit_signal(bus_, kWatcherPath, kWatcherIface,
                       "StatusNotifierItemUnregistered", "s",
                       (service + path).c_str());
    sd_bus_emit_properties_changed(bus_, kWatcherPath, kWatcherIface,
                                   "RegisteredStatusNotifierItems", nullptr);
    for (auto it = items_.begin(); it != items_.end(); ++it)
      if (it->service == service && it->path == path) {
        Item copy = std::move(*it);
        items_.erase(it);
        if (events_.itemRemoved) events_.itemRemoved(copy);
        break;
      }
  }

} // namespace bbai::sni
