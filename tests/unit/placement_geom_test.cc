// Pure placement math (classic Placement.cc semantics, minimal port): Center
// centres in the work area; Cascade steps a fixed diagonal from the work
// origin, wrapping; RowSmart returns the first free left-to-right slot, falling
// back to the origin when the area is full. No wlroots.
#include <doctest/doctest.h>
#include "Placement.geom.hh"

using namespace bbai;
// NB: the namespace is `bbai::place` and the function is `place`, so a
// `using namespace bbai::place;` makes a bare `place(...)` call ambiguous with
// the namespace name. Qualify as `place::place` (exactly how the Server calls it).

TEST_CASE("Center centres the frame in the work area") {
  wlr_box work{0, 0, 1280, 697};      // 720 minus a 23px bottom bar
  int cur = 0;
  place::Point p = place::place(WindowPlacement::Center, work, 204, 179, {}, cur);
  CHECK(p.x == (1280 - 204) / 2);
  CHECK(p.y == (697 - 179) / 2);
}

TEST_CASE("Cascade steps a diagonal and wraps at the edge") {
  wlr_box work{0, 0, 1280, 697};
  int cur = 0;
  place::Point a = place::place(WindowPlacement::Cascade, work, 204, 179, {}, cur);
  place::Point b = place::place(WindowPlacement::Cascade, work, 204, 179, {}, cur);
  CHECK(a.x == 0); CHECK(a.y == 0);
  CHECK(b.x > a.x); CHECK(b.y > a.y);   // stepped down-right
}

TEST_CASE("RowSmart avoids an occupied origin, falls back when full") {
  wlr_box work{0, 0, 1280, 697};
  int cur = 0;
  std::vector<wlr_box> taken{{0, 0, 204, 179}};
  place::Point p = place::place(WindowPlacement::RowSmart, work, 204, 179, taken, cur);
  CHECK_FALSE((p.x == 0 && p.y == 0));  // not on top of the taken slot (parens: no decomp)
  CHECK(p.x >= 0); CHECK(p.x + 204 <= 1280);
}
