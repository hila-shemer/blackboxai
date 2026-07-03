// StatusNotifierWatcher + Host over sd-bus. The compositor IS the tray (same
// stance as waybar/swaybar: embed the watcher, don't depend on one). We own
// org.kde.StatusNotifierWatcher, accept item registrations, materialize
// sni::Item's, and proxy activation clicks back to the item.
//
// Inert-never-fatal: no session bus, or another tray already owning the
// watcher name -> ok()==false and the compositor runs on without a tray.
#ifndef BLACKBOXAI_SNIHOST_HH
#define BLACKBOXAI_SNIHOST_HH

#include "Sni.hh"

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct wl_event_loop;
struct wl_event_source;
struct sd_bus;
struct sd_bus_slot;
struct sd_bus_track;

namespace bbai::sni {

  class Host {
  public:
    // loop may be null: tests then pump processForTest() by hand (the same
    // nullable-loop pattern as TimerRegistry, src/Timer.hh:52).
    explicit Host(wl_event_loop *loop);
    ~Host();
    Host(const Host &) = delete;
    Host &operator=(const Host &) = delete;

    bool ok() const { return bus_ != nullptr; }

    // Drain sd_bus_process exactly like the production fd source does.
    void processForTest();

  private:
    struct Cb;           // sd-bus / wl_event_loop C callbacks (SniHost.cc)
    friend struct Cb;

    void drain();        // process until idle, then re-arm fd/timer sources
    void teardownBus();  // drop sources + slots + connection (goes inert)

    wl_event_loop *loop_ = nullptr;
    wl_event_source *fd_source_ = nullptr;
    wl_event_source *timer_source_ = nullptr;
    sd_bus *bus_ = nullptr;
    sd_bus_slot *watcher_slot_ = nullptr;
  };

} // namespace bbai::sni

#endif // BLACKBOXAI_SNIHOST_HH
