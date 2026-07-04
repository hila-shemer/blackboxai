// The classic Blackbox Windowmenu as a pure builder over the two facts it
// needs from a View (its workspace, whether it is maximized, its handle) plus
// the workspace model. Kept View-free so it is L0-testable without a Server
// (View needs a live toplevel). The runtime path fills ViewFacts from a View
// in build(). Accepted parity gaps (documented once, in the wave-2 plan): no
// Shade (xdg-shell has none), no AlwaysOnTop/Bottom (program law - no
// layer-aware restack), no OccupyAll / vert-horz maximize / KillClient (no
// state or policy for them). Ships: Send To..., Iconify, Maximize, Close.
#ifndef BLACKBOXAI_WINDOWMENU_HH
#define BLACKBOXAI_WINDOWMENU_HH

#include "MenuItem.hh"
#include "Workspace.hh"

#include <vector>

namespace bbai {

  class View;

  namespace windowmenu {
    struct ViewFacts { unsigned workspace; bool maximized; void *handle; };

    std::vector<MenuItem> buildFrom(const ViewFacts &f, const WorkspaceModel &ws);
    // Convenience: pull ViewFacts off a live View (runtime path).
    std::vector<MenuItem> build(const View *v, const WorkspaceModel &ws);
  }

} // namespace bbai

#endif // BLACKBOXAI_WINDOWMENU_HH
