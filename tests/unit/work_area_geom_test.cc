// Work-area strut aggregation: max-per-edge across registrants, applied to a
// layout-coords box (classic BScreen::updateAvailableArea port). Pure math -
// the Output registry and the Toolbar registrant are system-tested separately.
#include <doctest/doctest.h>
#include "WorkArea.geom.hh"

#include <vector>

using namespace bbai;

TEST_CASE("no struts: work area == full box") {
  const wlr_box full{0, 0, 1280, 720};
  const std::vector<const Strut *> none;
  const wlr_box w = workarea::computeWorkArea(full, none);
  CHECK(w.x == 0); CHECK(w.y == 0);
  CHECK(w.width == 1280); CHECK(w.height == 720);
}

TEST_CASE("bottom strut lifts the floor (the toolbar case)") {
  const wlr_box full{0, 0, 1280, 720};
  Strut bar; bar.bottom = 23;
  const std::vector<const Strut *> struts{&bar};
  const wlr_box w = workarea::computeWorkArea(full, struts);
  CHECK(w.x == 0); CHECK(w.y == 0);
  CHECK(w.width == 1280); CHECK(w.height == 697);
}

TEST_CASE("overlapping struts: max per edge, not sum; origin offsets carry") {
  const wlr_box full{1280, 0, 1280, 720};   // a second head's layout box
  Strut a; a.top = 30; a.left = 10;
  Strut b; b.top = 12; b.right = 40;
  const std::vector<const Strut *> struts{&a, &b};
  const wlr_box w = workarea::computeWorkArea(full, struts);
  CHECK(w.x == 1290);        // full.x + max(left)
  CHECK(w.y == 30);          // full.y + max(top) - NOT 42
  CHECK(w.width == 1230);    // 1280 - 10 - 40
  CHECK(w.height == 690);    // 720 - 30
}

TEST_CASE("degenerate struts that swallow the output clamp to 1px") {
  const wlr_box full{0, 0, 100, 100};
  Strut greedy; greedy.left = 90; greedy.right = 90;
  greedy.top = 60; greedy.bottom = 60;
  const std::vector<const Strut *> struts{&greedy};
  const wlr_box w = workarea::computeWorkArea(full, struts);
  CHECK(w.width == 1);
  CHECK(w.height == 1);
}

TEST_CASE("registrant mutates its Strut in place - the registry sees it") {
  const wlr_box full{0, 0, 1280, 720};
  Strut s; s.bottom = 23;
  const std::vector<const Strut *> struts{&s};
  CHECK(workarea::computeWorkArea(full, struts).height == 697);
  s.bottom = 2;   // auto-hide sliver
  CHECK(workarea::computeWorkArea(full, struts).height == 718);
}
