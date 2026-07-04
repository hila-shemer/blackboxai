// Pure slit geometry - classic Slit.cc's sizing/reposition/strut/hidden math
// (reference/blackboxwm/src/Slit.cc:203-240,335-386,389-473) as header-only
// functions, plus the two pixel seams the icon path needs. One deliberate
// deviation from classic: items are uniform kSlot-sized cells (we render SNI
// icons, not arbitrary XEmbed dockapps). The strut matrix quirks are ported
// verbatim - see the unit test for the pinned weird cells.
#ifndef BLACKBOXAI_SLIT_GEOM_HH
#define BLACKBOXAI_SLIT_GEOM_HH

#include "Config.hh"          // SlitPlacement / SlitDirection (parsed wave-1)
#include "WorkArea.geom.hh"   // Strut

#include <cstdint>
#include <vector>

namespace bbai::slit {

  struct Rect { int x, y, w, h; };

  constexpr int kSlot = 24;   // icon cell edge - SNI icons, not 64px dockapps

  // margin = style slit.marginWidth, border = slitTexture().borderWidth().
  struct Metrics { int margin = 2; int border = 0; int slot = kSlot; };

  // Frame size for n items (classic sizing with uniform cells): the major
  // axis stacks cells with a margin between and around, the cross axis is
  // one cell plus border+margin on each side.
  inline Rect frameSize(int n, SlitDirection dir, const Metrics &m) {
    const int major = n * m.slot + m.margin * (n + 1) + 2 * m.border;
    const int cross = m.slot + 2 * (m.border + m.margin);
    if (dir == SlitDirection::Vertical) return {0, 0, cross, major};
    return {0, 0, major, cross};
  }

  inline Rect placeFrame(Rect size, SlitPlacement p, int OW, int OH) {
    int x = 0, y = 0;
    switch (p) {
    case SlitPlacement::TopLeft:      x = 0;                 y = 0;                 break;
    case SlitPlacement::CenterLeft:   x = 0;                 y = (OH - size.h) / 2; break;
    case SlitPlacement::BottomLeft:   x = 0;                 y = OH - size.h;       break;
    case SlitPlacement::TopCenter:    x = (OW - size.w) / 2; y = 0;                 break;
    case SlitPlacement::BottomCenter: x = (OW - size.w) / 2; y = OH - size.h;       break;
    case SlitPlacement::TopRight:     x = OW - size.w;       y = 0;                 break;
    case SlitPlacement::CenterRight:  x = OW - size.w;       y = (OH - size.h) / 2; break;
    case SlitPlacement::BottomRight:  x = OW - size.w;       y = OH - size.h;       break;
    }
    return {x, y, size.w, size.h};
  }

  // Auto-hidden position: slide off the anchored edge leaving a margin-wide
  // sliver. Only ONE axis slides - x for left/right-anchored placements, y
  // for Top/BottomCenter (classic quirk, ported as-is).
  inline Rect hiddenRect(Rect shown, SlitPlacement p, int margin, int OW, int OH) {
    Rect r = shown;
    switch (p) {
    case SlitPlacement::TopLeft:
    case SlitPlacement::CenterLeft:
    case SlitPlacement::BottomLeft:   r.x = margin - shown.w; break;
    case SlitPlacement::TopRight:
    case SlitPlacement::CenterRight:
    case SlitPlacement::BottomRight:  r.x = OW - margin;      break;
    case SlitPlacement::TopCenter:    r.y = margin - shown.h; break;
    case SlitPlacement::BottomCenter: r.y = OH - margin;      break;
    }
    return r;
  }

  // Cell i relative to the frame origin (classic: first cell at
  // border+margin, pitch = size + margin; cross-axis centering degenerates
  // to border+margin with uniform cells).
  inline Rect itemRect(int i, SlitDirection dir, const Metrics &m) {
    const int start = m.border + m.margin;
    const int pitch = m.slot + m.margin;
    if (dir == SlitDirection::Vertical) return {start, start + i * pitch, m.slot, m.slot};
    return {start + i * pitch, start, m.slot, m.slot};
  }

  // Cell index at a frame-relative point; -1 on the frame, in a gap, or out.
  inline int itemIndexAt(int rx, int ry, int n, SlitDirection dir, const Metrics &m) {
    const int cross = (dir == SlitDirection::Vertical) ? rx : ry;
    const int major = (dir == SlitDirection::Vertical) ? ry : rx;
    const int start = m.border + m.margin;
    if (cross < start || cross >= start + m.slot) return -1;
    const int rel = major - start;
    if (rel < 0) return -1;
    const int pitch = m.slot + m.margin;
    const int i = rel / pitch;
    if (i >= n || rel % pitch >= m.slot) return -1;
    return i;
  }

  // Classic toolbar-overlap avoidance: an intersecting slit shifts vertically
  // by the toolbar's exposed height; up when the slit's bottom is at or above
  // the bar's, down otherwise. Returns the (possibly unchanged) y.
  inline int toolbarShift(Rect s, Rect tb, int tb_exposed) {
    const bool intersects = !(s.x + s.w <= tb.x || tb.x + tb.w <= s.x ||
                              s.y + s.h <= tb.y || tb.y + tb.h <= s.y);
    if (!intersects) return s.y;
    int delta = tb_exposed;
    if (s.y + s.h <= tb.y + tb.h) delta = -delta;
    return s.y + delta;
  }

  // The classic strut matrix, exclusive-coordinate port (classic used
  // inclusive rect.bottom(); we reserve OH - y, the same strip). The exposed
  // width/height only shrink to the margin on the direction's OWN axis
  // (Slit.cc:90-103) - that asymmetry produces the quirk cells the unit test
  // pins; do not "fix" them.
  inline Strut strutFor(SlitDirection dir, SlitPlacement p, Rect shown, Rect hidden,
                        bool auto_hide, const Metrics &m, int OH) {
    const int ew = (dir == SlitDirection::Vertical && auto_hide) ? m.margin : shown.w;
    const int eh = (dir == SlitDirection::Horizontal && auto_hide) ? m.margin : shown.h;
    Strut s{};
    if (dir == SlitDirection::Vertical) {
      switch (p) {
      case SlitPlacement::TopCenter:    s.top = eh;    break;   // FULL h: quirk
      case SlitPlacement::BottomCenter: s.bottom = eh; break;   // FULL h: quirk
      case SlitPlacement::TopLeft:
      case SlitPlacement::CenterLeft:
      case SlitPlacement::BottomLeft:   s.left = ew;   break;
      case SlitPlacement::TopRight:
      case SlitPlacement::CenterRight:
      case SlitPlacement::BottomRight:  s.right = ew;  break;
      }
    } else {
      switch (p) {
      case SlitPlacement::TopLeft:
      case SlitPlacement::TopCenter:
      case SlitPlacement::TopRight:     s.top = shown.y + eh; break;
      case SlitPlacement::BottomLeft:
      case SlitPlacement::BottomCenter:
      case SlitPlacement::BottomRight:
        s.bottom = OH - (auto_hide ? hidden.y : shown.y);      break;
      case SlitPlacement::CenterLeft:   s.left = ew;           break;  // FULL w: quirk
      case SlitPlacement::CenterRight:  s.right = ew;          break;
      }
    }
    return s;
  }

  // --- pixel seams (pure, POC-verified math) --------------------------------

  // SNI icon bytes are STRAIGHT alpha; wlroots' default blend mode is
  // premultiplied (wlr/render/pass.h). Convert once at the render seam.
  inline void premultiply(std::vector<uint32_t> &px) {
    for (uint32_t &p : px) {
      const uint32_t a = p >> 24;
      const uint32_t r = (((p >> 16) & 0xFF) * a) / 255;
      const uint32_t g = (((p >> 8) & 0xFF) * a) / 255;
      const uint32_t b = ((p & 0xFF) * a) / 255;
      p = (a << 24) | (r << 16) | (g << 8) | b;
    }
  }

  // CPU nearest-neighbor to a t x t cell - renderer-independent, so the
  // golden raster is byte-stable across pixman/wlroots bumps.
  inline std::vector<uint32_t> nearestScale(const std::vector<uint32_t> &src,
                                            int sw, int sh, int t) {
    std::vector<uint32_t> out(static_cast<std::size_t>(t) * t, 0);
    if (sw <= 0 || sh <= 0 || src.size() < static_cast<std::size_t>(sw) * sh)
      return out;
    for (int y = 0; y < t; ++y)
      for (int x = 0; x < t; ++x)
        out[static_cast<std::size_t>(y) * t + x] =
          src[static_cast<std::size_t>(y * sh / t) * sw + (x * sw / t)];
    return out;
  }

} // namespace bbai::slit

#endif // BLACKBOXAI_SLIT_GEOM_HH
