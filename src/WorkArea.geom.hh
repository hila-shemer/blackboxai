// Per-output work area: registrant-owned struts, max-per-edge aggregation
// (classic BScreen::updateAvailableArea). Header-only pure math like
// Toolbar.geom.hh; the registry lives on Output. wlr.hpp is included for
// wlr_box only.
#ifndef BLACKBOXAI_WORKAREA_GEOM_HH
#define BLACKBOXAI_WORKAREA_GEOM_HH

#include "wlr.hpp"

#include <algorithm>
#include <span>

namespace bbai {

  // One edge-reservation. The registrant OWNS its Strut: register the pointer
  // once, mutate the fields in place on change, then call
  // Output::strutsChanged(). Four edges because the wave-2 slit needs
  // left/right (classic Slit::updateStrut); the toolbar only ever sets
  // top/bottom.
  struct Strut { int left = 0, top = 0, right = 0, bottom = 0; };

  namespace workarea {

    // `full` minus the max (not sum) of each edge across all struts, clamped
    // so a pathological strut set can't produce a non-positive box.
    inline wlr_box computeWorkArea(wlr_box full,
                                   std::span<const Strut *const> struts) {
      int l = 0, t = 0, r = 0, b = 0;
      for (const Strut *s : struts) {
        l = std::max(l, s->left);
        t = std::max(t, s->top);
        r = std::max(r, s->right);
        b = std::max(b, s->bottom);
      }
      wlr_box w;
      w.x = full.x + l;
      w.y = full.y + t;
      w.width  = full.width  - l - r;
      w.height = full.height - t - b;
      if (w.width  < 1) w.width  = 1;
      if (w.height < 1) w.height = 1;
      return w;
    }

  } // namespace workarea
} // namespace bbai

#endif // BLACKBOXAI_WORKAREA_GEOM_HH
