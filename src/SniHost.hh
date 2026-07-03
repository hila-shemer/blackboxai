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

  struct HostEvents {
    std::function<void(const Item&)> itemAdded, itemChanged, itemRemoved;
  };

  class Host {
  public:
    // loop may be null: tests then pump processForTest() by hand (the same
    // nullable-loop pattern as TimerRegistry, src/Timer.hh:52).
    explicit Host(wl_event_loop *loop);
    ~Host();
    Host(const Host &) = delete;
    Host &operator=(const Host &) = delete;

    bool ok() const { return bus_ != nullptr; }

    const std::vector<Item> &items() const { return items_; }
    void setEvents(HostEvents ev) { events_ = std::move(ev); }

    // Fire-and-forget async calls on the item; replies ignored (v1). The
    // dbusmenu behind a menu-only item's ContextMenu is wave-2 menus.
    void activate(const Item &, int x, int y);
    void secondaryActivate(const Item &, int x, int y);
    void contextMenu(const Item &, int x, int y);

    // Drain sd_bus_process exactly like the production fd source does.
    void processForTest();

  private:
    struct Cb;           // sd-bus / wl_event_loop C callbacks (SniHost.cc)
    friend struct Cb;

    void drain();        // process until idle, then re-arm fd/timer sources
    void teardownBus();  // drop sources + slots + connection (goes inert)

    // One accepted registration. The D-Bus-visible watcher state derives from
    // this list; materialized items (task 5) lag it by one GetAll round trip.
    // unique_ptr because Reg* is handed to sd-bus as userdata - reallocation
    // must not move entries. `owner` is the registrant's unique name: change
    // signals arrive from it even when `service` is a well-known name.
    struct Reg;
    void addRegistration(const std::string &service, const std::string &path,
                         const std::string &owner);

    void fetchAll(Reg &reg);           // async Properties.GetAll -> Cb::onGetAll
    void callItem(const Item &, const char *method, int x, int y);
    void storeItem(Item item);         // upsert + fire itemAdded/itemChanged
    void dropRegistration(const std::string &service, const std::string &path);

    std::vector<std::unique_ptr<Reg>> regs_;
    bool host_registered_ = false;   // any StatusNotifierHost announced (task 9: us)
    std::vector<Item> items_;
    HostEvents events_;

    wl_event_loop *loop_ = nullptr;
    wl_event_source *fd_source_ = nullptr;
    wl_event_source *timer_source_ = nullptr;
    sd_bus *bus_ = nullptr;
    sd_bus_slot *watcher_slot_ = nullptr;
  };

} // namespace bbai::sni

#endif // BLACKBOXAI_SNIHOST_HH
