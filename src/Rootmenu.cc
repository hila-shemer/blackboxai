#include "Rootmenu.hh"
#include "Text.hh"

namespace bbai::rootmenu {

  namespace {
    MenuItem exec(const char *label, std::vector<std::string> argv) {
      MenuItem m;
      m.label = bt::decodeUtf8(label);
      m.action = MenuItem::Act::Exec;
      m.argv = std::move(argv);
      return m;
    }
    MenuItem separator() {
      MenuItem m;
      m.kind = MenuItem::Kind::Separator;
      return m;
    }
    MenuItem simple(const char *label, MenuItem::Act act) {
      MenuItem m;
      m.label = bt::decodeUtf8(label);
      m.action = act;
      return m;
    }
    MenuItem option(const char *label, ConfigOption opt, bool checked, bool enabled) {
      MenuItem m;
      m.label = bt::decodeUtf8(label);
      m.action = MenuItem::Act::ConfigOption;
      m.option = opt;
      m.checked = checked;
      m.enabled = enabled;
      return m;
    }
  } // namespace

  std::u32string title() { return bt::decodeUtf8("Blackbox"); }

  std::vector<MenuItem> buildWorkspacesSubmenu(const WorkspaceModel &ws) {
    std::vector<MenuItem> sub;
    for (unsigned i = 0; i < ws.count(); ++i) {
      MenuItem m; m.label = bt::decodeUtf8(ws.name(i).c_str());
      m.action = MenuItem::Act::WorkspaceSwitch; m.workspace = i;
      m.checked = (i == ws.current());
      sub.push_back(std::move(m));
    }
    sub.push_back(separator());
    sub.push_back(simple("New Workspace", MenuItem::Act::NewWorkspace));
    sub.push_back(simple("Remove Last Workspace", MenuItem::Act::RemoveWorkspace));
    return sub;
  }

  std::vector<MenuItem> buildConfigSubmenu(const Config &cfg) {
    std::vector<MenuItem> items;

    // Classic ConfigFocusmenu: CTF/Sloppy are radios; AutoRaise/ClickRaise
    // only mean anything under sloppy, so they are disabled otherwise.
    MenuItem focus;
    focus.kind = MenuItem::Kind::Submenu;
    focus.label = bt::decodeUtf8("Focus Model");
    const bool sloppy = (cfg.focusModel == FocusModel::SloppyFocus);
    focus.submenu_items.push_back(
        option("Click to Focus", ConfigOption::FocusClickToFocus, !sloppy, true));
    focus.submenu_items.push_back(
        option("Sloppy Focus", ConfigOption::FocusSloppy, sloppy, true));
    focus.submenu_items.push_back(
        option("Auto Raise", ConfigOption::AutoRaise, cfg.autoRaise, sloppy));
    focus.submenu_items.push_back(
        option("Click Raise", ConfigOption::ClickRaise, cfg.clickRaise, sloppy));
    items.push_back(std::move(focus));

    // Classic ConfigPlacementmenu, radios only - the direction rows need
    // machinery we don't have (documented omission).
    MenuItem place;
    place.kind = MenuItem::Kind::Submenu;
    place.label = bt::decodeUtf8("Window Placement");
    const WindowPlacement wp = cfg.windowPlacement;
    place.submenu_items.push_back(option("Smart Placement (Rows)",
        ConfigOption::PlacementRowSmart, wp == WindowPlacement::RowSmart, true));
    place.submenu_items.push_back(option("Smart Placement (Columns)",
        ConfigOption::PlacementColSmart, wp == WindowPlacement::ColSmart, true));
    place.submenu_items.push_back(option("Center Placement",
        ConfigOption::PlacementCenter, wp == WindowPlacement::Center, true));
    place.submenu_items.push_back(option("Cascade Placement",
        ConfigOption::PlacementCascade, wp == WindowPlacement::Cascade, true));
    items.push_back(std::move(place));

    items.push_back(separator());
    items.push_back(option("Focus New Windows", ConfigOption::FocusNewWindows,
                           cfg.focusNewWindows, true));
    return items;
  }

  std::vector<MenuItem> build(const WorkspaceModel &ws) {
    std::vector<MenuItem> items;
    items.push_back(exec("kitty", {"kitty"}));
    items.push_back(exec("xterm", {"xterm"}));
    items.push_back(separator());
    MenuItem wsm;
    wsm.kind = MenuItem::Kind::Submenu;
    wsm.label = bt::decodeUtf8("Workspaces");
    wsm.submenu_items = buildWorkspacesSubmenu(ws);
    items.push_back(std::move(wsm));
    items.push_back(separator());
    items.push_back(simple("Restart", MenuItem::Act::Restart));  // stub in M4
    items.push_back(simple("Exit", MenuItem::Act::Exit));
    return items;
  }

  namespace {
    void fixupDynamic(std::vector<MenuItem> &items, const WorkspaceModel &ws,
                      const Config &cfg) {
      for (MenuItem &m : items) {
        if (m.kind != MenuItem::Kind::Submenu) continue;
        if (m.action == MenuItem::Act::WorkspacesMenu) {
          m.submenu_items = buildWorkspacesSubmenu(ws);
        } else if (m.action == MenuItem::Act::ConfigMenu) {
          m.enabled = true;                        // wave-2 mount: live rows
          m.submenu_items = buildConfigSubmenu(cfg);
        } else {
          fixupDynamic(m.submenu_items, ws, cfg);
        }
      }
    }
  } // namespace

  std::vector<MenuItem> buildFromParsed(const std::vector<MenuItem> &parsed,
                                        const WorkspaceModel &ws,
                                        const Config &cfg) {
    if (parsed.empty()) return build(ws);   // locked: fall back to the RICHER in-code menu
    std::vector<MenuItem> items = parsed;
    fixupDynamic(items, ws, cfg);
    return items;
  }

} // namespace bbai::rootmenu
