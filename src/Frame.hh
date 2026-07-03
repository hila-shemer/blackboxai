// Blackbox window-frame geometry — pure integer math, no wlroots, so both the
// decoration renderer (src/Decoration.cc) and the input hit-test (src/Server.cc)
// share one source of truth, and it can be unit-tested directly.
//
// Pinned M3 defaults (title font textHeight 15, all textures bevel1/borderWidth 0):
//   border 1, titlebar 23, handle 6, grip 38, button/label 19, title margin 2.
// See docs/superpowers/plans/2026-06-14-blackboxai-m3-decorations.md §"Frame
// geometry" for the derivation. All rects are relative to the frame-tree origin
// (the frame's top-left, placed at the View's pos_x/pos_y).
#ifndef BLACKBOXAI_FRAME_HH
#define BLACKBOXAI_FRAME_HH

namespace bbai::frame {

  constexpr int kBorder       = 1;
  constexpr int kTitleHeight  = 23;
  constexpr int kHandleHeight = 6;
  constexpr int kGripWidth    = 38;
  constexpr int kButtonWidth  = 19;
  constexpr int kLabelHeight  = 19;
  constexpr int kTitleMargin  = 2;                       // by: top inset in titlebar
  constexpr int kButtonStep   = kButtonWidth + kTitleMargin;  // bwid = 21

  // Style-computed frame metrics. Defaults are the pinned M3 constants, so a
  // default-constructed FrameMetrics reproduces today's geometry exactly (the
  // builtin style relies on that - see Style::builtin()). titleMargin is the
  // EFFECTIVE inset: texture borderWidth + marginWidth, folded at load time.
  struct FrameMetrics {
    int border       = kBorder;        // 1
    int titleHeight  = kTitleHeight;   // 23
    int handleHeight = kHandleHeight;  // 6
    int gripWidth    = kGripWidth;     // 38
    int buttonWidth  = kButtonWidth;   // 19
    int labelHeight  = kLabelHeight;   // 19
    int titleMargin  = kTitleMargin;   // 2
    int buttonStep() const { return buttonWidth + titleMargin; }
  };

  struct Rect { int x, y, w, h; };

  inline int frameWidth(int W, const FrameMetrics &m = {})  { return W + 2 * m.border; }
  inline int frameHeight(int H, const FrameMetrics &m = {}) { return H + m.titleHeight + m.handleHeight; }
  inline int clientX(const FrameMetrics &m = {}) { return m.border; }
  inline int clientY(const FrameMetrics &m = {}) { return m.titleHeight; }

  inline Rect title(int W, int /*H*/, const FrameMetrics &m = {})
  { return { 0, 0, frameWidth(W, m), m.titleHeight }; }
  inline Rect handle(int W, int H, const FrameMetrics &m = {})
  { return { 0, m.titleHeight + H, frameWidth(W, m), m.handleHeight }; }
  inline Rect leftGrip(int /*W*/, int H, const FrameMetrics &m = {})
  { return { 0, m.titleHeight + H, m.gripWidth, m.handleHeight }; }
  inline Rect rightGrip(int W, int H, const FrameMetrics &m = {})
  { return { frameWidth(W, m) - m.gripWidth, m.titleHeight + H, m.gripWidth, m.handleHeight }; }

  // Titlebar children march in from the edges; iconify left, then close
  // (rightmost) and maximize to its left, label fills the middle.
  inline Rect iconifyButton(int /*W*/, int /*H*/, const FrameMetrics &m = {})
  { return { m.titleMargin, m.titleMargin, m.buttonWidth, m.buttonWidth }; }
  inline Rect closeButton(int W, int /*H*/, const FrameMetrics &m = {})
  { return { frameWidth(W, m) - m.buttonStep(), m.titleMargin, m.buttonWidth, m.buttonWidth }; }
  inline Rect maximizeButton(int W, int /*H*/, const FrameMetrics &m = {})
  { return { frameWidth(W, m) - 2 * m.buttonStep(), m.titleMargin, m.buttonWidth, m.buttonWidth }; }

  inline Rect label(int W, int /*H*/, const FrameMetrics &m = {}) {
    const int lx = m.titleMargin + m.buttonStep();          // after iconify
    int label_w = frameWidth(W, m) - 2 * m.titleMargin - 3 * m.buttonStep();
    if (label_w < 1) label_w = 1;
    return { lx, m.titleMargin, label_w, m.labelHeight };
  }

  // The two border columns flanking the client area (top/bottom edges are
  // covered by the titlebar/handle textures).
  inline Rect leftBorder(int /*W*/, int H, const FrameMetrics &m = {})
  { return { 0, m.titleHeight, m.border, H }; }
  inline Rect rightBorder(int W, int H, const FrameMetrics &m = {})
  { return { frameWidth(W, m) - m.border, m.titleHeight, m.border, H }; }

} // namespace bbai::frame

#endif // BLACKBOXAI_FRAME_HH
