// Pure workspace data model for M4 — the count, the current index, per-workspace
// names ("Workspace N", blackbox's default), and per-workspace focus memory.
// Distilled from blackboxwm src/Workspace.{hh,cc} (which is X/Screen-coupled): the
// window lists + stacking live in the compositor (StackingList + the Views); this
// model is pure (no wlroots) so it is L0-unit-testable. Focus is stored as an
// opaque handle (a View* in the compositor, void* here).
#ifndef BLACKBOXAI_WORKSPACE_HH
#define BLACKBOXAI_WORKSPACE_HH

#include <string>
#include <vector>

namespace bbai {

  class WorkspaceModel {
  public:
    explicit WorkspaceModel(unsigned count = 4);

    unsigned count(void) const { return static_cast<unsigned>(ws.size()); }
    unsigned current(void) const { return current_; }
    void setCurrent(unsigned i);             // i must be < count()

    // "Workspace N" (N = i+1) unless a name was set (M5 config); empty resets.
    std::string name(unsigned i) const;
    void setName(unsigned i, const std::string &new_name);

    // Per-workspace focus memory (opaque handle, e.g. a View*).
    void setFocused(unsigned i, void *handle);
    void *focused(unsigned i) const;
    void clearFocused(void *handle);         // drop `handle` from every workspace

    unsigned addWorkspace(void);             // append one; returns its index
    void removeLastWorkspace(void);          // never below 1; clamps current_

  private:
    struct WS {
      std::string name;            // empty => the "Workspace N" default
      void *focused = nullptr;
    };
    std::vector<WS> ws;
    unsigned current_ = 0;
  };

} // namespace bbai

#endif // BLACKBOXAI_WORKSPACE_HH
