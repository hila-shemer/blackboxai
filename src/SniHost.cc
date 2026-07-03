#include "SniHost.hh"

#include "wlr.hpp"   // wl_event_loop/-source (the only sanctioned wayland include)

#include <systemd/sd-bus.h>

#include <cstdio>

namespace bbai::sni {

  namespace {
    constexpr const char *kWatcherName = "org.kde.StatusNotifierWatcher";
    constexpr const char *kWatcherPath  = "/StatusNotifierWatcher";
    constexpr const char *kWatcherIface = "org.kde.StatusNotifierWatcher";
    constexpr const char *kItemIface    = "org.kde.StatusNotifierItem";
  }

  struct Host::Reg {
    Host *host = nullptr;
    std::string service, path, owner;
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
      if (r->service == service && r->path == path)
        return;                       // re-registration: task 5 turns this into a re-fetch
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
  }

} // namespace bbai::sni
