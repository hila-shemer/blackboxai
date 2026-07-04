// The menu item model (ported from blackboxwm lib/Menu MenuItem, with the M4
// action payload that the X11 version kept in Rootmenu's sidecar funcmap). Pure
// data; the runtime Menu (B8) renders + hit-tests these.
#ifndef BLACKBOXAI_MENUITEM_HH
#define BLACKBOXAI_MENUITEM_HH

#include <string>
#include <vector>

namespace bbai {

  // Configuration-menu knobs (wave-2 seam contract). Radio values encode the
  // target state; toggle values name the knob to flip. APPEND-ONLY at the
  // tail - the menus slice adds its Toolbar*/Slit* values after ours, and
  // Server::setConfigOption's switch grows in the same per-slice groups.
  enum class ConfigOption {
    FocusClickToFocus, FocusSloppy, AutoRaise, ClickRaise, FocusNewWindows,
    PlacementRowSmart, PlacementColSmart, PlacementCenter, PlacementCascade
  };

  struct MenuItem {
    enum class Kind { Command, Separator, Submenu };
    enum class Act {
      None, Exec, WorkspaceSwitch, NewWorkspace, RemoveWorkspace, Restart, Exit,
      Deiconify,
      // M5 menu-file actions:
      SetStyle,        // argv[0] = tilde-expanded style file path
      Reconfigure,     // classic [reconfig]; a documented {cmd} is dropped + noted
      RestartOther,    // argv = {"/bin/sh", "-c", "exec " + cmd}
      WorkspacesMenu,  // marker on the [workspaces] placeholder Submenu
      ConfigMenu,      // marker on the [config] placeholder Submenu (wave-2 mount)
      ConfigOption,    // Configuration-submenu row -> Server::setConfigOption
      // wave-2 menus (Windowmenu + dbusmenu). target = View handle for the
      // window ops (re-validated via viewForHandle, icon-menu precedent);
      // workspace = target ws (SendToWorkspace) or dbusmenu item id (DbusmenuEvent).
      Iconify, MaximizeToggle, Close, SendToWorkspace, DbusmenuEvent
    };

    std::u32string label;
    Kind kind = Kind::Command;
    Act  action = Act::None;
    std::vector<std::string> argv;          // for Exec (argv[0] is the program)
    unsigned workspace = ~0u;               // target for WorkspaceSwitch
    void *target = nullptr;                 // Deiconify: handle to the View
    ConfigOption option = ConfigOption::FocusClickToFocus;  // valid iff action == Act::ConfigOption
    bool enabled = true;
    bool checked = false;                   // e.g. the current workspace
    std::vector<MenuItem> submenu_items;    // non-empty for Kind::Submenu (cascade)

    bool separator() const { return kind == Kind::Separator; }
    bool selectable() const { return kind != Kind::Separator && enabled; }
  };

} // namespace bbai

#endif // BLACKBOXAI_MENUITEM_HH
