// Pure region-screenshot geometry: drag-corner normalization, output clamping,
// and the four GNOME-style dim rects that frame the clear selection. No wlroots.
#ifndef BLACKBOXAI_SCREENSHOT_GEOM_HH
#define BLACKBOXAI_SCREENSHOT_GEOM_HH

#include <initializer_list>   // range-for over a braced pointer list below

namespace bbai::screenshot {

  struct Rect { int x = 0, y = 0, w = 0, h = 0; };
  struct DimRects { Rect above, below, left, right; };

  // Two opposite drag corners -> a positive-size rect (any drag direction).
  inline Rect fromCorners(int ax, int ay, int bx, int by) {
    const int x = ax < bx ? ax : bx;
    const int y = ay < by ? ay : by;
    const int w = ax < bx ? bx - ax : ax - bx;
    const int h = ay < by ? by - ay : ay - by;
    return { x, y, w, h };
  }

  // Clamp to [0,ow] x [0,oh]; never returns a negative width/height.
  inline Rect clampToOutput(Rect s, int ow, int oh) {
    if (s.x < 0) { s.w += s.x; s.x = 0; }
    if (s.y < 0) { s.h += s.y; s.y = 0; }
    if (s.x > ow) s.x = ow;
    if (s.y > oh) s.y = oh;
    if (s.x + s.w > ow) s.w = ow - s.x;
    if (s.y + s.h > oh) s.h = oh - s.y;
    if (s.w < 0) s.w = 0;
    if (s.h < 0) s.h = 0;
    return s;
  }

  // Four rects tiling the output minus `sel`: full-width bands above/below,
  // selection-height strips left/right. Degenerate bands collapse to zero area.
  inline DimRects dimRects(int ow, int oh, Rect sel) {
    const int selBottom = sel.y + sel.h;
    const int selRight  = sel.x + sel.w;
    DimRects d;
    d.above = { 0, 0,         ow,             sel.y };
    d.below = { 0, selBottom, ow,             oh - selBottom };
    d.left  = { 0, sel.y,     sel.x,          sel.h };
    d.right = { selRight, sel.y, ow - selRight, sel.h };
    for (Rect *r : { &d.above, &d.below, &d.left, &d.right }) {
      if (r->w < 0) r->w = 0;
      if (r->h < 0) r->h = 0;
    }
    return d;
  }

} // namespace bbai::screenshot

#endif // BLACKBOXAI_SCREENSHOT_GEOM_HH
