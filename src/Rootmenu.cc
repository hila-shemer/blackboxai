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
    void fixupDynamic(std::vector<MenuItem> &items, const WorkspaceModel &ws) {
      for (MenuItem &m : items) {
        if (m.kind != MenuItem::Kind::Submenu) continue;
        if (m.action == MenuItem::Act::WorkspacesMenu) {
          m.submenu_items = buildWorkspacesSubmenu(ws);
        } else if (m.action == MenuItem::Act::ConfigMenu) {
          m.enabled = false;                 // wave-2 configmenu mounts here
          m.submenu_items.clear();
        } else {
          fixupDynamic(m.submenu_items, ws);
        }
      }
    }
  } // namespace

  std::vector<MenuItem> buildFromParsed(const std::vector<MenuItem> &parsed,
                                        const WorkspaceModel &ws) {
    if (parsed.empty()) return build(ws);   // locked: fall back to the RICHER in-code menu
    std::vector<MenuItem> items = parsed;
    fixupDynamic(items, ws);
    return items;
  }

} // namespace bbai::rootmenu
