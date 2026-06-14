// Pure toolbar geometry — section-rect math, header-only like Frame.hh, so both
// the toolbar renderer and the input hit-test share one source of truth and it is
// L0-unit-testable without fcft. Section widths are computed from a passed-in text
// width (labelWidth / windowLabelWidth); the golden PNG is the pixel source of
// truth — no rendered-width constants are baked in here. Geometry derived from
// blackboxwm src/Toolbar.cc:288-349 (the per-section `extra`-subtraction tiling).
//
// Pinned M4 defaults (BottomCenter, 66% width): bar 23 tall, sections 19 tall,
// frame margin 2, button 19, derived from blackbox's textHeight=15 convention
// (rendered fcft text height 18 fits the 19px section, as in M3).
#ifndef BLACKBOXAI_TOOLBAR_GEOM_HH
#define BLACKBOXAI_TOOLBAR_GEOM_HH

namespace bbai::toolbar {

  constexpr int kFrameMargin  = 2;
  constexpr int kLabelMargin  = 2;
  constexpr int kLabelHeight  = 19;
  constexpr int kButtonWidth  = 19;
  constexpr int kBarHeight     = 23;
  constexpr int kHiddenHeight  = 2;    // M5 auto-hide; unused in M4
  constexpr int kWidthPercent  = 66;
  constexpr int kExtra         = 6;    // overlap-compaction when frame_margin>0

  struct Rect { int x, y, w, h; };
  struct Sections {
    Rect workspace_label, prev_ws, next_ws, window_label, prev_win, next_win, clock;
  };

  inline int barWidth(int OW) { return OW * kWidthPercent / 100; }

  // The bar's on-screen rect (output coords); BottomCenter.
  inline Rect barRect(int OW, int OH) {
    const int bw = barWidth(OW);
    return { (OW - bw) / 2, OH - kBarHeight, bw, kBarHeight };
  }

  // Equalized workspace-label / clock width = widest text + 2*margin.
  inline int labelWidth(int max_text_w) { return max_text_w + kLabelMargin * 2; }

  // The window-label section fills the remaining interior (Toolbar.cc:297).
  inline int windowLabelWidth(int bar_w, int label_w, int clock_w) {
    const int w = bar_w - (clock_w + kButtonWidth * 4 + label_w + kFrameMargin * 8)
                + kExtra * 6;
    return w < 1 ? 1 : w;
  }

  // All section rects relative to the bar origin, given the (runtime-computed)
  // workspace-label width and clock width (blackbox keeps them equal).
  inline Sections sectionRects(int bar_w, int label_w, int clock_w) {
    const int fm = kFrameMargin, ex = kExtra, bwb = kButtonWidth;
    const int y = kFrameMargin, h = kLabelHeight;
    const int wl = windowLabelWidth(bar_w, label_w, clock_w);
    Sections s;
    s.workspace_label = { fm,                                         y, label_w, h };
    s.prev_ws         = { fm * 2 + label_w - ex,                      y, bwb,     h };
    s.next_ws         = { fm * 3 + label_w + bwb - ex * 2,            y, bwb,     h };
    s.window_label    = { fm * 4 + bwb * 2 + label_w - ex * 3,        y, wl,      h };
    s.prev_win        = { fm * 5 + bwb * 2 + label_w + wl - ex * 4,   y, bwb,     h };
    s.next_win        = { fm * 6 + bwb * 3 + label_w + wl - ex * 5,   y, bwb,     h };
    s.clock           = { bar_w - clock_w - fm,                       y, clock_w, h };
    return s;
  }

} // namespace bbai::toolbar

#endif // BLACKBOXAI_TOOLBAR_GEOM_HH
