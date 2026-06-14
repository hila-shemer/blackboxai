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

  struct Rect { int x, y, w, h; };

  inline int frameWidth(int W)  { return W + 2 * kBorder; }
  inline int frameHeight(int H) { return H + kTitleHeight + kHandleHeight; }
  inline int clientX() { return kBorder; }       // 1
  inline int clientY() { return kTitleHeight; }   // 23

  inline Rect title(int W, int /*H*/)  { return { 0, 0, frameWidth(W), kTitleHeight }; }
  inline Rect handle(int W, int H)     { return { 0, kTitleHeight + H, frameWidth(W), kHandleHeight }; }
  inline Rect leftGrip(int W, int H)   { return { 0, kTitleHeight + H, kGripWidth, kHandleHeight }; }
  inline Rect rightGrip(int W, int H)  { return { frameWidth(W) - kGripWidth, kTitleHeight + H, kGripWidth, kHandleHeight }; }

  // Titlebar children march in from the edges; iconify left, then close
  // (rightmost) and maximize to its left, label fills the middle.
  inline Rect iconifyButton(int /*W*/, int /*H*/) { return { kTitleMargin, kTitleMargin, kButtonWidth, kButtonWidth }; }
  inline Rect closeButton(int W, int /*H*/)       { return { frameWidth(W) - kButtonStep,     kTitleMargin, kButtonWidth, kButtonWidth }; }
  inline Rect maximizeButton(int W, int /*H*/)    { return { frameWidth(W) - 2 * kButtonStep, kTitleMargin, kButtonWidth, kButtonWidth }; }

  inline Rect label(int W, int /*H*/) {
    const int lx = kTitleMargin + kButtonStep;            // after iconify
    int label_w = frameWidth(W) - 2 * kTitleMargin - 3 * kButtonStep;  // minus iconify+close+max+by
    if (label_w < 1) label_w = 1;
    return { lx, kTitleMargin, label_w, kLabelHeight };
  }

  // The two 1px border columns flanking the client area (top/bottom edges are
  // covered by the titlebar/handle textures).
  inline Rect leftBorder(int /*W*/, int H)  { return { 0, kTitleHeight, kBorder, H }; }
  inline Rect rightBorder(int W, int H)     { return { frameWidth(W) - kBorder, kTitleHeight, kBorder, H }; }

} // namespace bbai::frame

#endif // BLACKBOXAI_FRAME_HH
