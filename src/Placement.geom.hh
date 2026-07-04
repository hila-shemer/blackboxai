// Window auto-placement math (classic Placement.cc, minimal port). Header-only
// pure geometry like Toolbar.geom.hh / WorkArea.geom.hh - no wlroots state, no
// scene. The Server feeds it the work area, the frame size and the set of
// already-placed frames on the target workspace; onViewMapped applies the
// result. wlr.hpp is included for wlr_box only; Config.hh for WindowPlacement.
#ifndef BLACKBOXAI_PLACEMENT_GEOM_HH
#define BLACKBOXAI_PLACEMENT_GEOM_HH

#include "wlr.hpp"
#include "Config.hh"   // bbai::WindowPlacement

#include <vector>

namespace bbai::place {

  struct Point { int x, y; };

  // Fixed diagonal step for Cascade (classic used the title height; a constant
  // keeps the geometry test independent of the live style).
  inline constexpr int kCascadeStep = 24;

  inline bool intersects(wlr_box a, wlr_box b) {
    return a.x < b.x + b.width && b.x < a.x + a.width &&
           a.y < b.y + b.height && b.y < a.y + a.height;
  }

  // Place a fw x fh frame in `work` per policy. `taken` are the frames already
  // occupying the target workspace; `cascade_cursor` is the caller-owned step
  // counter for Cascade (ignored by the others), advanced here.
  inline Point place(WindowPlacement policy, wlr_box work, int fw, int fh,
                     const std::vector<wlr_box> &taken, int &cascade_cursor) {
    switch (policy) {
    case WindowPlacement::Center:
      return { work.x + (work.width  - fw) / 2,
               work.y + (work.height - fh) / 2 };

    case WindowPlacement::Cascade: {
      int n = cascade_cursor;
      int x = work.x + n * kCascadeStep;
      int y = work.y + n * kCascadeStep;
      // Wrap back to the work origin once the diagonal would run off an edge.
      if (x + fw > work.x + work.width || y + fh > work.y + work.height) {
        n = 0; x = work.x; y = work.y;
      }
      cascade_cursor = n + 1;
      return { x, y };
    }

    case WindowPlacement::RowSmart:
    case WindowPlacement::ColSmart:
    default: {
      // First free left-to-right, top-to-bottom slot in fw/fh strides that no
      // taken frame intersects; fall back to the origin when the area is full.
      for (int y = work.y; y + fh <= work.y + work.height; y += fh) {
        for (int x = work.x; x + fw <= work.x + work.width; x += fw) {
          const wlr_box cand{ x, y, fw, fh };
          bool clash = false;
          for (const wlr_box &t : taken)
            if (intersects(cand, t)) { clash = true; break; }
          if (!clash) return { x, y };
        }
      }
      return { work.x, work.y };
    }
    }
  }

} // namespace bbai::place

#endif // BLACKBOXAI_PLACEMENT_GEOM_HH
