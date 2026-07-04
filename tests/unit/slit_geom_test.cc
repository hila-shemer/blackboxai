// Wave-2 slit: pure geometry ported from classic Slit.cc, with uniform
// slot-sized cells (SNI icons) instead of arbitrary dockapp sizes. The strut
// matrix quirks (Vertical+TopCenter reserving FULL height under auto-hide,
// Bottom{Left,Right} never sliding on y) are classic behavior, pinned here
// on purpose - port, don't fix.
#include <doctest/doctest.h>
#include "Slit.geom.hh"

using namespace bbai;
using namespace bbai::slit;

static const Metrics kM{2, 1, 24};   // margin 2 (classic default), border 1, slot 24

TEST_CASE("frameSize: classic sizing with uniform slot cells") {
  // Vertical, 2 items: w = slot + 2*(border+margin), h = 2*slot + margin*(n+1) + 2*border
  Rect v = frameSize(2, SlitDirection::Vertical, kM);
  CHECK(v.w == 30);
  CHECK(v.h == 56);
  // One item: a square frame.
  Rect one = frameSize(1, SlitDirection::Vertical, kM);
  CHECK(one.w == 30);
  CHECK(one.h == 30);
  // Horizontal mirrors.
  Rect hz = frameSize(2, SlitDirection::Horizontal, kM);
  CHECK(hz.w == 56);
  CHECK(hz.h == 30);
}

TEST_CASE("placeFrame: all 8 placements") {
  const Rect s{0, 0, 30, 56};
  Rect r = placeFrame(s, SlitPlacement::CenterRight, 1280, 720);
  CHECK(r.x == 1250); CHECK(r.y == 332);
  r = placeFrame(s, SlitPlacement::TopLeft, 1280, 720);
  CHECK(r.x == 0); CHECK(r.y == 0);
  r = placeFrame(s, SlitPlacement::CenterLeft, 1280, 720);
  CHECK(r.x == 0); CHECK(r.y == 332);
  r = placeFrame(s, SlitPlacement::BottomLeft, 1280, 720);
  CHECK(r.x == 0); CHECK(r.y == 664);
  r = placeFrame(s, SlitPlacement::TopCenter, 1280, 720);
  CHECK(r.x == 625); CHECK(r.y == 0);
  r = placeFrame(s, SlitPlacement::BottomCenter, 1280, 720);
  CHECK(r.x == 625); CHECK(r.y == 664);
  r = placeFrame(s, SlitPlacement::TopRight, 1280, 720);
  CHECK(r.x == 1250); CHECK(r.y == 0);
  r = placeFrame(s, SlitPlacement::BottomRight, 1280, 720);
  CHECK(r.x == 1250); CHECK(r.y == 664);
}

TEST_CASE("hiddenRect: one axis slides, margin sliver stays (classic Slit.cc:389-461)") {
  // Right column: slide right, leave `margin` on-screen.
  Rect h = hiddenRect({1250, 332, 30, 56}, SlitPlacement::CenterRight, 2, 1280, 720);
  CHECK(h.x == 1278); CHECK(h.y == 332);
  // Left column: x = margin - width.
  h = hiddenRect({0, 664, 30, 56}, SlitPlacement::BottomLeft, 2, 1280, 720);
  CHECK(h.x == -28); CHECK(h.y == 664);
  // TopCenter slides UP on y; x stays.
  h = hiddenRect({625, 0, 30, 56}, SlitPlacement::TopCenter, 2, 1280, 720);
  CHECK(h.x == 625); CHECK(h.y == -54);
  // BottomCenter slides DOWN on y.
  h = hiddenRect({612, 690, 56, 30}, SlitPlacement::BottomCenter, 2, 1280, 720);
  CHECK(h.y == 718);
}

TEST_CASE("itemRect + itemIndexAt round-trip") {
  Rect r0 = itemRect(0, SlitDirection::Vertical, kM);
  CHECK(r0.x == 3); CHECK(r0.y == 3); CHECK(r0.w == 24); CHECK(r0.h == 24);
  Rect r1 = itemRect(1, SlitDirection::Vertical, kM);
  CHECK(r1.x == 3); CHECK(r1.y == 29);
  Rect h1 = itemRect(1, SlitDirection::Horizontal, kM);
  CHECK(h1.x == 29); CHECK(h1.y == 3);

  CHECK(itemIndexAt(10, 30, 2, SlitDirection::Vertical, kM) == 1);
  CHECK(itemIndexAt(10, 10, 2, SlitDirection::Vertical, kM) == 0);
  CHECK(itemIndexAt(10, 28, 2, SlitDirection::Vertical, kM) == -1);  // gap between cells
  CHECK(itemIndexAt(1, 10, 2, SlitDirection::Vertical, kM) == -1);   // frame border
  CHECK(itemIndexAt(10, 60, 2, SlitDirection::Vertical, kM) == -1);  // past the last cell
  CHECK(itemIndexAt(30, 10, 2, SlitDirection::Horizontal, kM) == 1);
}

TEST_CASE("toolbarShift: classic overlap avoidance, sign flips (Slit.cc:442-453)") {
  const Rect tb{218, 697, 844, 23};   // BottomCenter toolbar on 1280x720
  // BottomCenter slit overlaps the bar; slit bottom <= bar bottom -> shift UP.
  CHECK(toolbarShift({625, 664, 30, 56}, tb, 23) == 641);
  // No intersection -> unchanged.
  CHECK(toolbarShift({1250, 332, 30, 56}, tb, 23) == 332);
  // Slit hanging BELOW the bar's bottom -> shift DOWN.
  CHECK(toolbarShift({625, 690, 30, 56}, tb, 23) == 713);
}

TEST_CASE("strutFor: the full classic matrix incl. the weird cells (Slit.cc:335-386)") {
  const Rect vshown{1250, 332, 30, 56};
  const Rect vhidden{1278, 332, 30, 56};
  Strut s = strutFor(SlitDirection::Vertical, SlitPlacement::CenterRight,
                     vshown, vhidden, false, kM, 720);
  CHECK(s.right == 30); CHECK(s.left == 0); CHECK(s.top == 0); CHECK(s.bottom == 0);
  s = strutFor(SlitDirection::Vertical, SlitPlacement::CenterRight,
               vshown, vhidden, true, kM, 720);
  CHECK(s.right == 2);                       // auto-hide sliver
  s = strutFor(SlitDirection::Vertical, SlitPlacement::CenterLeft,
               {0, 332, 30, 56}, {-28, 332, 30, 56}, true, kM, 720);
  CHECK(s.left == 2);
  // THE classic quirk: Vertical + TopCenter reserves the FULL height even
  // auto-hidden (exposedHeight only shrinks for Horizontal, Slit.cc:98-103).
  s = strutFor(SlitDirection::Vertical, SlitPlacement::TopCenter,
               {625, 0, 30, 56}, {625, -54, 30, 56}, true, kM, 720);
  CHECK(s.top == 56);
  s = strutFor(SlitDirection::Vertical, SlitPlacement::BottomCenter,
               {625, 664, 30, 56}, {625, 718, 30, 56}, true, kM, 720);
  CHECK(s.bottom == 56);

  // Horizontal: Top cells reserve y + exposed; Bottom cells from screen bottom.
  const Rect hshown{612, 690, 56, 30};
  const Rect hhidden{612, 718, 56, 30};
  s = strutFor(SlitDirection::Horizontal, SlitPlacement::BottomCenter,
               hshown, hhidden, false, kM, 720);
  CHECK(s.bottom == 30);
  s = strutFor(SlitDirection::Horizontal, SlitPlacement::BottomCenter,
               hshown, hhidden, true, kM, 720);
  CHECK(s.bottom == 2);                      // hidden y = 718 -> 720-718
  // Bottom{Left,Right} never slide on y -> full strut even auto-hidden (quirk).
  s = strutFor(SlitDirection::Horizontal, SlitPlacement::BottomLeft,
               {0, 690, 56, 30}, {-54, 690, 56, 30}, true, kM, 720);
  CHECK(s.bottom == 30);
  s = strutFor(SlitDirection::Horizontal, SlitPlacement::TopLeft,
               {0, 0, 56, 30}, {-54, 0, 56, 30}, true, kM, 720);
  CHECK(s.top == 2);                         // y(0) + margin
  s = strutFor(SlitDirection::Horizontal, SlitPlacement::TopLeft,
               {0, 0, 56, 30}, {-54, 0, 56, 30}, false, kM, 720);
  CHECK(s.top == 30);
  // Horizontal + CenterLeft auto-hidden reserves the FULL width (quirk:
  // exposedWidth only shrinks for Vertical, Slit.cc:90-95).
  s = strutFor(SlitDirection::Horizontal, SlitPlacement::CenterLeft,
               {0, 345, 56, 30}, {-54, 345, 56, 30}, true, kM, 720);
  CHECK(s.left == 56);
}

TEST_CASE("premultiply: SNI straight alpha -> wlroots premultiplied") {
  // The POC-verified vector: 0x80FF0000 -> 0x80800000 (255*128/255 == 128).
  std::vector<uint32_t> px{0x80FF0000u, 0xFF102030u, 0x00FFFFFFu, 0x80102030u};
  premultiply(px);
  CHECK(px[0] == 0x80800000u);
  CHECK(px[1] == 0xFF102030u);   // opaque unchanged
  CHECK(px[2] == 0x00000000u);   // fully transparent zeroes out
  CHECK(px[3] == 0x80081018u);   // 16,32,48 at alpha 128 -> 8,16,24
}

TEST_CASE("nearestScale: 2x2 -> 4x4 quadrants, degenerate input -> zeros") {
  std::vector<uint32_t> src{1, 2, 3, 4};
  std::vector<uint32_t> out = nearestScale(src, 2, 2, 4);
  REQUIRE(out.size() == 16);
  CHECK(out[0] == 1); CHECK(out[3] == 2);
  CHECK(out[5] == 1); CHECK(out[6] == 2);
  CHECK(out[12] == 3); CHECK(out[15] == 4);
  CHECK(nearestScale({}, 0, 0, 4) == std::vector<uint32_t>(16, 0));
}
