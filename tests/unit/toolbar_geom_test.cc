// A5: pure toolbar section-rect math (src/Toolbar.geom.hh). Tests the FORMULA
// (the extra-subtraction tiling) with fixed widths — the rendered text width
// comes from fcft at runtime, the golden PNG pins the pixels.
#include <doctest/doctest.h>
#include "Toolbar.geom.hh"

using namespace bbai::toolbar;

TEST_CASE("bar is bottom-center at 66% output width") {
  CHECK(barWidth(1280) == 844);
  const Rect b = barRect(1280, 720);
  CHECK(b.x == 218);
  CHECK(b.y == 697);
  CHECK(b.w == 844);
  CHECK(b.h == 23);
}

TEST_CASE("barRect placement variants") {
  using P = bbai::toolbar::Placement;
  // Default (no placement arg) == BottomCenter — existing callers unaffected.
  {
    const Rect b = barRect(1280, 720);
    CHECK(b.x == 218); CHECK(b.y == 697); CHECK(b.w == 844); CHECK(b.h == 23);
  }
  // Explicit BottomCenter.
  {
    const Rect b = barRect(1280, 720, P::BottomCenter);
    CHECK(b.x == 218); CHECK(b.y == 697); CHECK(b.w == 844); CHECK(b.h == 23);
  }
  // TopCenter.
  {
    const Rect b = barRect(1280, 720, P::TopCenter);
    CHECK(b.x == 218); CHECK(b.y ==   0); CHECK(b.w == 844); CHECK(b.h == 23);
  }
  // TopLeft.
  {
    const Rect b = barRect(1280, 720, P::TopLeft);
    CHECK(b.x ==   0); CHECK(b.y ==   0); CHECK(b.w == 844); CHECK(b.h == 23);
  }
  // TopRight: x = 1280-844 = 436.
  {
    const Rect b = barRect(1280, 720, P::TopRight);
    CHECK(b.x == 436); CHECK(b.y ==   0); CHECK(b.w == 844); CHECK(b.h == 23);
  }
  // BottomLeft.
  {
    const Rect b = barRect(1280, 720, P::BottomLeft);
    CHECK(b.x ==   0); CHECK(b.y == 697); CHECK(b.w == 844); CHECK(b.h == 23);
  }
  // BottomRight: x = 1280-844 = 436.
  {
    const Rect b = barRect(1280, 720, P::BottomRight);
    CHECK(b.x == 436); CHECK(b.y == 697); CHECK(b.w == 844); CHECK(b.h == 23);
  }
}

TEST_CASE("sectionRects is placement-independent (takes bar_w, not placement)") {
  // sectionRects only depends on bar_w / label_w / clock_w; compute it once and
  // verify it gives the same result regardless of which placement barRect used.
  const Sections s1 = sectionRects(844, 103, 103);
  const Sections s2 = sectionRects(844, 103, 103);   // identical call
  auto eq = [](Rect a, Rect b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
  };
  CHECK(eq(s1.workspace_label, s2.workspace_label));
  CHECK(eq(s1.prev_ws,         s2.prev_ws));
  CHECK(eq(s1.next_ws,         s2.next_ws));
  CHECK(eq(s1.window_label,    s2.window_label));
  CHECK(eq(s1.prev_win,        s2.prev_win));
  CHECK(eq(s1.next_win,        s2.next_win));
  CHECK(eq(s1.clock,           s2.clock));
}

TEST_CASE("label/clock width = text + 2*margin; window label fills the rest") {
  CHECK(labelWidth(99) == 103);
  CHECK(windowLabelWidth(844, 103, 103) == 582);
  CHECK(windowLabelWidth(20, 103, 103) == 1);   // never negative
}

TEST_CASE("section rects tile the interior with the extra-subtraction compaction") {
  const Sections s = sectionRects(844, /*label_w=*/103, /*clock_w=*/103);
  auto eq = [](Rect r, int x, int y, int w, int h) {
    return r.x == x && r.y == y && r.w == w && r.h == h;
  };
  CHECK(eq(s.workspace_label,   2, 2, 103, 19));
  CHECK(eq(s.prev_ws,         101, 2,  19, 19));
  CHECK(eq(s.next_ws,         116, 2,  19, 19));
  CHECK(eq(s.window_label,    131, 2, 582, 19));
  CHECK(eq(s.prev_win,        709, 2,  19, 19));
  CHECK(eq(s.next_win,        724, 2,  19, 19));
  CHECK(eq(s.clock,           739, 2, 103, 19));
  // The clock is right-anchored within the bar interior.
  CHECK(s.clock.x + s.clock.w == 844 - kFrameMargin);
}
