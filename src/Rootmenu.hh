// The built-in M4 root-menu tree (flat: exec items + inline workspace entries +
// Restart/Exit). Drop-in menu-file parsing + cascade submenus are M5.
#ifndef BLACKBOXAI_ROOTMENU_HH
#define BLACKBOXAI_ROOTMENU_HH

#include "MenuItem.hh"
#include "Workspace.hh"

#include <string>
#include <vector>

namespace bbai::rootmenu {

  std::u32string title();
  // Built from the live workspace model so the entries + the current-✓ stay correct.
  std::vector<MenuItem> build(const WorkspaceModel &ws);

} // namespace bbai::rootmenu

#endif // BLACKBOXAI_ROOTMENU_HH
