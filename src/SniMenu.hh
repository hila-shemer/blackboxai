// The com.canonical.dbusmenu CLIENT for a menu-only tray item. Rides the
// sni::Host's existing sd_bus (slots share its fd source), fetches the layout
// async and parses the recursive (ia{sv}av) tree straight into MenuItems. v1
// renders text only (no icon column in bt::Menu - locked): icon-name/icon-data
// are parsed-past. Owns its slots so destroying it (closeMenus) cancels any
// in-flight reply - the "reply after the user moved on" drop is structural.
#ifndef BLACKBOXAI_SNIMENU_HH
#define BLACKBOXAI_SNIMENU_HH

#include "MenuItem.hh"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct sd_bus;
struct sd_bus_slot;
struct sd_bus_message;

namespace bbai {

  class SniMenu {
  public:
    // on_layout(ok, items): ok=false on a GetLayout error (no dbusmenu server
    // at the path) - the caller then falls back to the SNI ContextMenu proxy.
    // Called again on every accepted LayoutUpdated (rebuild in place).
    SniMenu(sd_bus *bus, std::string service, std::string path,
            std::function<void(bool, std::vector<MenuItem>)> on_layout);
    ~SniMenu();
    SniMenu(const SniMenu &) = delete;
    SniMenu &operator=(const SniMenu &) = delete;

    // AboutToShow(0) -> GetLayout(0,-1,{}). AboutToShow first: some apps
    // (electron) return an empty root until it is processed. Also arms the
    // LayoutUpdated match.
    void open();
    // Event(id,"clicked",<y:0>,0) fire-and-forget. Queued on the shared bus
    // before we return, so it survives this SniMenu being torn down by the
    // very next closeMenus.
    void sendClicked(int id);

  private:
    void fetchLayout();
    void failNow();   // async send failed: no slot armed -> report like an error reply
    static int onAboutToShow(sd_bus_message *, void *, void *);
    static int onLayout(sd_bus_message *, void *, void *);
    static int onLayoutUpdated(sd_bus_message *, void *, void *);
    int parseNode(sd_bus_message *, MenuItem &out, bool &visible);

    sd_bus *bus_;
    std::string service_, path_;
    std::function<void(bool, std::vector<MenuItem>)> on_layout_;
    sd_bus_slot *about_slot_ = nullptr;
    sd_bus_slot *layout_slot_ = nullptr;
    sd_bus_slot *sig_slot_ = nullptr;
    uint32_t last_revision_ = 0;
    bool have_revision_ = false;
  };

} // namespace bbai

#endif // BLACKBOXAI_SNIMENU_HH
