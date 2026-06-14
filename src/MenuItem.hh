// The menu item model (ported from blackboxwm lib/Menu MenuItem, with the M4
// action payload that the X11 version kept in Rootmenu's sidecar funcmap). Pure
// data; the runtime Menu (B8) renders + hit-tests these.
#ifndef BLACKBOXAI_MENUITEM_HH
#define BLACKBOXAI_MENUITEM_HH

#include <string>
#include <vector>

namespace bbai {

  class Menu;

  struct MenuItem {
    enum class Kind { Command, Separator, Submenu };
    enum class Act {
      None, Exec, WorkspaceSwitch, NewWorkspace, RemoveWorkspace, Restart, Exit
    };

    std::u32string label;
    Kind kind = Kind::Command;
    Act  action = Act::None;
    std::vector<std::string> argv;   // for Exec (argv[0] is the program)
    unsigned workspace = ~0u;        // target for WorkspaceSwitch
    bool enabled = true;
    bool checked = false;            // e.g. the current workspace
    Menu *sub = nullptr;             // non-null for Kind::Submenu (cascade)

    bool separator() const { return kind == Kind::Separator; }
    bool selectable() const { return kind != Kind::Separator && enabled; }
  };

} // namespace bbai

#endif // BLACKBOXAI_MENUITEM_HH
