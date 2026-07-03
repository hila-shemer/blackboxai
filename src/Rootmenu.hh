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
  // Switch rows for every workspace + separator + New + Remove Last.
  // Used as the child items for the Workspaces submenu (F3.2+).
  std::vector<MenuItem> buildWorkspacesSubmenu(const WorkspaceModel &ws);

  // Live fixup of a parsed menu-file tree: [workspaces] placeholders get the
  // real workspace submenu (rebuilt per open, so the current-mark stays
  // honest), [config] placeholders go disabled until wave-2 configmenu mounts
  // there, and an empty parse falls back to the in-code menu - a broken first
  // boot still gets a terminal + workspaces, not classic's bare xterm stub.
  std::vector<MenuItem> buildFromParsed(const std::vector<MenuItem> &parsed,
                                        const WorkspaceModel &ws);

} // namespace bbai::rootmenu

#endif // BLACKBOXAI_ROOTMENU_HH
