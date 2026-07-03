#include "SniHost.hh"

#include "wlr.hpp"   // wl_event_loop/-source (the only sanctioned wayland include)

#include <systemd/sd-bus.h>

#include <cstdio>

namespace bbai::sni {

  namespace {
    constexpr const char *kWatcherName = "org.kde.StatusNotifierWatcher";
  }

  Host::Host(wl_event_loop *loop) : loop_(loop) {
    sd_bus *bus = nullptr;
    if (sd_bus_open_user(&bus) < 0) {
      // No session bus (bare TTY login, headless CI): tray off, compositor fine.
      return;
    }
    // Claim the tray. flags 0 = don't queue, don't steal: if another tray owns
    // the watcher name we go inert rather than fight it.
    int r = sd_bus_request_name(bus, kWatcherName, 0);
    if (r < 0) {
      fprintf(stderr, "blackboxai: SNI watcher name taken (%d) - tray disabled\n", r);
      sd_bus_flush_close_unref(bus);
      return;
    }
    bus_ = bus;
  }

  Host::~Host() { teardownBus(); }

  void Host::teardownBus() {
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

} // namespace bbai::sni
