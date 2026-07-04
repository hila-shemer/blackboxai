#include "Windowmenu.hh"
#include "View.hh"
#include "Text.hh"

namespace bbai::windowmenu {

  std::vector<MenuItem> buildFrom(const ViewFacts &f, const WorkspaceModel &ws) {
    std::vector<MenuItem> items;

    MenuItem send;
    send.kind = MenuItem::Kind::Submenu;
    send.label = bt::decodeUtf8("Send To...");
    for (unsigned i = 0; i < ws.count(); ++i) {
      MenuItem m;
      m.label = bt::decodeUtf8(ws.name(i).c_str());
      m.action = MenuItem::Act::SendToWorkspace;
      m.workspace = i;
      m.target = f.handle;
      m.checked = (i == f.workspace);         // classic: current row checked...
      m.enabled = (i != f.workspace);         // ...and disabled (nowhere to send)
      send.submenu_items.push_back(std::move(m));
    }
    items.push_back(std::move(send));

    auto op = [&](const char *label, MenuItem::Act act, bool checked = false) {
      MenuItem m;
      m.label = bt::decodeUtf8(label);
      m.action = act;
      m.target = f.handle;
      m.checked = checked;
      return m;
    };
    items.push_back([]{ MenuItem s; s.kind = MenuItem::Kind::Separator; return s; }());
    items.push_back(op("Iconify", MenuItem::Act::Iconify));
    items.push_back(op("Maximize", MenuItem::Act::MaximizeToggle, f.maximized));
    items.push_back([]{ MenuItem s; s.kind = MenuItem::Kind::Separator; return s; }());
    items.push_back(op("Close", MenuItem::Act::Close));
    return items;
  }

  std::vector<MenuItem> build(const View *v, const WorkspaceModel &ws) {
    return buildFrom({ v->workspace(), v->isMaximized(), v->windowID() }, ws);
  }

} // namespace bbai::windowmenu
