#include "SniMenu.hh"
#include "Text.hh"

#include <systemd/sd-bus.h>

#include <cstring>
#include <utility>

namespace bbai {

  namespace {
    // "__" -> "_"; a single '_' marks the access key and is stripped. Order
    // matters (do the escape while scanning). Sway's in-place loop is the ref.
    std::string stripMnemonics(const std::string &in) {
      std::string out; out.reserve(in.size());
      for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '_') {
          if (i + 1 < in.size() && in[i + 1] == '_') { out += '_'; ++i; }
          // else: single '_' dropped (mnemonic marker)
        } else out += in[i];
      }
      return out;
    }
  } // namespace

  SniMenu::SniMenu(sd_bus *bus, std::string service, std::string path,
                   std::function<void(bool, std::vector<MenuItem>)> on_layout)
    : bus_(bus), service_(std::move(service)), path_(std::move(path)),
      on_layout_(std::move(on_layout)) {}

  SniMenu::~SniMenu() {
    // Unref cancels any in-flight reply callback - a reply landing after the
    // menu closed finds no slot and is dropped (no use-after-free on `this`).
    if (about_slot_)  sd_bus_slot_unref(about_slot_);
    if (layout_slot_) sd_bus_slot_unref(layout_slot_);
    if (sig_slot_)    sd_bus_slot_unref(sig_slot_);
  }

  void SniMenu::open() {
    // Arm the update match first (before anything can emit), then AboutToShow.
    sd_bus_match_signal_async(
        bus_, &sig_slot_, service_.c_str(), path_.c_str(),
        "com.canonical.dbusmenu", "LayoutUpdated",
        reinterpret_cast<sd_bus_message_handler_t>(&SniMenu::onLayoutUpdated),
        nullptr, this);
    sd_bus_call_method_async(
        bus_, &about_slot_, service_.c_str(), path_.c_str(),
        "com.canonical.dbusmenu", "AboutToShow",
        reinterpret_cast<sd_bus_message_handler_t>(&SniMenu::onAboutToShow),
        this, "i", 0);
    // Force the parked async call out NOW. The Host's fd mask was last set by
    // its own drain (POLLIN-only on an idle bus), so an enqueue here would wait
    // for unrelated inbound traffic to flush - forever on a quiet bus (the same
    // hazard Host::callItem defeats with a post-send drain).
    sd_bus_flush(bus_);
  }

  int SniMenu::onAboutToShow(sd_bus_message *, void *userdata, void *) {
    // Ignore needUpdate + errors: fetch the layout either way (electron returns
    // an empty root until AboutToShow is processed - we always follow up).
    static_cast<SniMenu *>(userdata)->fetchLayout();
    return 0;
  }

  void SniMenu::fetchLayout() {
    if (layout_slot_) layout_slot_ = (sd_bus_slot_unref(layout_slot_), nullptr);
    sd_bus_call_method_async(
        bus_, &layout_slot_, service_.c_str(), path_.c_str(),
        "com.canonical.dbusmenu", "GetLayout",
        reinterpret_cast<sd_bus_message_handler_t>(&SniMenu::onLayout),
        this, "iias", 0, -1, 0);   // parent 0, recursionDepth -1, empty propNames
    sd_bus_flush(bus_);            // flush now (idle-bus hazard, see open())
  }

  int SniMenu::onLayout(sd_bus_message *m, void *userdata, void *) {
    auto *self = static_cast<SniMenu *>(userdata);
    // Copy the callback out first: the error/empty branch resets the owning
    // sni_menu_ (this SniMenu) from INSIDE the call, which would otherwise free
    // the std::function while it runs (delete-this-in-callback). The local copy
    // keeps the target alive for the whole invocation; sd-bus defers the unref
    // of the slot currently dispatching, so tearing the SniMenu down here is safe.
    auto cb = self->on_layout_;
    if (sd_bus_message_is_method_error(m, nullptr)) {
      cb(false, {});                        // no dbusmenu here -> caller proxies
      return 0;
    }
    uint32_t rev = 0;
    if (sd_bus_message_read(m, "u", &rev) < 0) { cb(false, {}); return 0; }
    self->last_revision_ = rev;
    self->have_revision_ = true;
    MenuItem root; bool vis = true;
    if (self->parseNode(m, root, vis) < 0) { cb(false, {}); return 0; }
    cb(true, std::move(root.submenu_items));   // root's kids ARE the menu
    return 0;
  }

  int SniMenu::onLayoutUpdated(sd_bus_message *m, void *userdata, void *) {
    auto *self = static_cast<SniMenu *>(userdata);
    uint32_t rev = 0; int parent = 0;
    sd_bus_message_read(m, "ui", &rev, &parent);
    // Gate on a real revision bump: udiskie emits LayoutUpdated after every
    // AboutToShow, so a naive refetch-on-signal loops (sway PR #5161).
    if (!self->have_revision_ || rev != self->last_revision_) self->fetchLayout();
    return 0;
  }

  // (ia{sv}av) -> MenuItem. Filters happen in the parent (visible=false kids
  // are dropped). Icons (icon-name/icon-data) are parsed-past: text-only v1.
  int SniMenu::parseNode(sd_bus_message *m, MenuItem &out, bool &visible) {
    int r = sd_bus_message_enter_container(m, 'r', "ia{sv}av");
    if (r < 0) return r;
    int id = 0;
    if ((r = sd_bus_message_read(m, "i", &id)) < 0) return r;
    out.action = MenuItem::Act::DbusmenuEvent;
    out.workspace = static_cast<unsigned>(id);   // dbusmenu id, reused payload
    visible = true;
    std::string type, label;
    int toggle_state = -1;
    bool children_display = false;

    if ((r = sd_bus_message_enter_container(m, 'a', "{sv}")) < 0) return r;
    while (sd_bus_message_at_end(m, 0) == 0) {
      if ((r = sd_bus_message_enter_container(m, 'e', "sv")) < 0) return r;
      const char *key = nullptr;
      if ((r = sd_bus_message_read(m, "s", &key)) < 0) return r;
      if (!strcmp(key, "label")) {
        const char *s; if ((r = sd_bus_message_read(m, "v", "s", &s)) < 0) return r; label = s;
      } else if (!strcmp(key, "type")) {
        const char *s; if ((r = sd_bus_message_read(m, "v", "s", &s)) < 0) return r; type = s;
      } else if (!strcmp(key, "enabled")) {
        int b; if ((r = sd_bus_message_read(m, "v", "b", &b)) < 0) return r; out.enabled = b;
      } else if (!strcmp(key, "visible")) {
        int b; if ((r = sd_bus_message_read(m, "v", "b", &b)) < 0) return r; visible = b;
      } else if (!strcmp(key, "toggle-state")) {
        int i; if ((r = sd_bus_message_read(m, "v", "i", &i)) < 0) return r; toggle_state = i;
      } else if (!strcmp(key, "children-display")) {
        const char *s; if ((r = sd_bus_message_read(m, "v", "s", &s)) < 0) return r;
        children_display = !strcmp(s, "submenu");
      } else {
        if ((r = sd_bus_message_skip(m, "v")) < 0) return r;   // unknown / icon keys
      }
      if ((r = sd_bus_message_exit_container(m)) < 0) return r;   // e
    }
    if ((r = sd_bus_message_exit_container(m)) < 0) return r;     // a{sv}

    if (type == "separator") out.kind = MenuItem::Kind::Separator;
    out.label = bt::decodeUtf8(stripMnemonics(label).c_str());
    out.checked = (toggle_state == 1);

    if ((r = sd_bus_message_enter_container(m, 'a', "v")) < 0) return r;
    std::vector<MenuItem> kids;
    while (sd_bus_message_at_end(m, 0) == 0) {
      if ((r = sd_bus_message_enter_container(m, 'v', "(ia{sv}av)")) < 0) return r;
      MenuItem kid; bool kv = true;
      if ((r = parseNode(m, kid, kv)) < 0) return r;
      if (kv) kids.push_back(std::move(kid));    // visible=false -> filtered
      if ((r = sd_bus_message_exit_container(m)) < 0) return r;   // v
    }
    if ((r = sd_bus_message_exit_container(m)) < 0) return r;     // av

    if ((children_display || !kids.empty()) && out.kind != MenuItem::Kind::Separator) {
      out.kind = MenuItem::Kind::Submenu;
      out.submenu_items = std::move(kids);
    }
    return sd_bus_message_exit_container(m);                      // r
  }

  void SniMenu::sendClicked(int id) {
    sd_bus_call_method_async(bus_, nullptr, service_.c_str(), path_.c_str(),
                             "com.canonical.dbusmenu", "Event", nullptr, nullptr,
                             "isvu", id, "clicked", "y",
                             static_cast<uint8_t>(0), static_cast<uint32_t>(0));
    sd_bus_flush(bus_);   // push the fire-and-forget Event out before we are torn down
  }

} // namespace bbai
