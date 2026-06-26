#include <doctest/doctest.h>
#include "Screenshot.geom.hh"

using namespace bbai::screenshot;

static long area(const Rect &r) { return (long)r.w * r.h; }
static bool overlaps(const Rect &a, const Rect &b) {
  return a.x < b.x + b.w && b.x < a.x + a.w &&
         a.y < b.y + b.h && b.y < a.y + a.h;
}

TEST_CASE("fromCorners normalizes any drag direction") {
  CHECK(fromCorners(10, 20, 110, 220).x == 10);
  CHECK(fromCorners(10, 20, 110, 220).w == 100);
  CHECK(fromCorners(110, 220, 10, 20).x == 10);   // dragged up-left
  CHECK(fromCorners(110, 220, 10, 20).w == 100);
  CHECK(fromCorners(110, 220, 10, 20).h == 200);
}

TEST_CASE("clampToOutput keeps the selection inside the output") {
  Rect c = clampToOutput({ -10, -10, 50, 50 }, 1280, 720);
  CHECK(c.x == 0); CHECK(c.y == 0); CHECK(c.w == 40); CHECK(c.h == 40);
  Rect d = clampToOutput({ 1260, 700, 100, 100 }, 1280, 720);
  CHECK(d.w == 20); CHECK(d.h == 20);
  Rect off = clampToOutput({ 2000, 2000, 50, 50 }, 1280, 720);
  CHECK(off.w == 0); CHECK(off.h == 0);            // fully off-screen
}

TEST_CASE("dimRects tile the complement of an interior selection") {
  const int OW = 1280, OH = 720;
  Rect sel{ 200, 150, 400, 300 };
  DimRects d = dimRects(OW, OH, sel);
  // No dim rect overlaps the selection.
  for (const Rect *r : { &d.above, &d.below, &d.left, &d.right })
    CHECK_FALSE(overlaps(*r, sel));
  // The four dim rects do not overlap each other.
  CHECK_FALSE(overlaps(d.above, d.left));
  CHECK_FALSE(overlaps(d.above, d.right));
  CHECK_FALSE(overlaps(d.below, d.left));
  CHECK_FALSE(overlaps(d.below, d.right));
  CHECK_FALSE(overlaps(d.left,  d.right));
  // Areas sum to output minus selection (exact tiling).
  long sum = area(d.above) + area(d.below) + area(d.left) + area(d.right);
  CHECK(sum == (long)OW * OH - area(sel));
}

TEST_CASE("dimRects degenerate cases produce no negative rects") {
  DimRects full = dimRects(1280, 720, { 0, 0, 1280, 720 });   // whole output
  for (const Rect *r : { &full.above, &full.below, &full.left, &full.right }) {
    CHECK(r->w >= 0); CHECK(r->h >= 0); CHECK(area(*r) == 0);
  }
  DimRects edge = dimRects(1280, 720, { 0, 0, 400, 300 });    // top-left corner
  CHECK(area(edge.above) == 0);   // nothing above
  CHECK(area(edge.left)  == 0);   // nothing left
  long sum = area(edge.above) + area(edge.below) + area(edge.left) + area(edge.right);
  CHECK(sum == (long)1280 * 720 - 400 * 300);
}
