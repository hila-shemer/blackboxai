// B6: pure single-column menu layout math (src/Menu.geom.hh) with mock widths.
#include <doctest/doctest.h>
#include "Menu.geom.hh"

using namespace bbai::menu;

TEST_CASE("per-item metrics") {
  CHECK(itemHeight(18, /*sep=*/false) == 20);   // max(18,16)+2
  CHECK(itemHeight(18, /*sep=*/true)  == kSeparatorHeight);
  CHECK(itemWidth(90) == 124);                  // 90 + 2*(16+1)
  CHECK(titleHeight(18) == 20);
}

TEST_CASE("single-column layout with a title and a separator") {
  std::vector<ItemMetric> items = {
    { 36, false },   // "foot"
    { 45, false },   // "xterm"
    {  0, true  },   // separator
    { 90, false },   // "Workspaces"
  };
  const Layout L = computeLayout(items, /*item_text_h=*/18,
                                 /*show_title=*/true, /*title_text_w=*/72, /*title_text_h=*/18);

  // width = max item width (124) + 2*frameMargin; title (74) loses.
  CHECK(L.width == 126);
  CHECK(L.title_h == 20);

  auto eq = [](Rect r, int x, int y, int w, int h) {
    return r.x == x && r.y == y && r.w == w && r.h == h;
  };
  CHECK(eq(L.items[0], 1, 21, 124, 20));        // first item below title (20) + frameMargin(1)
  CHECK(eq(L.items[1], 1, 41, 124, 20));
  CHECK(eq(L.items[2], 1, 61, 124, 3));         // separator
  CHECK(eq(L.items[3], 1, 64, 124, 20));
  CHECK(L.height == 85);                         // 64 + 20 + frameMargin

  // hit-test: a y inside item 0; separator and title yield -1.
  CHECK(itemAt(L, 30, items) == 0);
  CHECK(itemAt(L, 50, items) == 1);
  CHECK(itemAt(L, 62, items) == -1);            // separator row
  CHECK(itemAt(L, 5, items)  == -1);            // title region
  CHECK(itemAt(L, 999, items) == -1);           // below the menu
}

TEST_CASE("computeLayout: margins are parameters (MenuLook), default to the pinned 1") {
  using namespace bbai::menu;
  std::vector<ItemMetric> items{ {40, false}, {60, false} };

  // Default call == pinned constants (byte-identical to the shipped goldens).
  Layout d = computeLayout(items, 18, /*show_title=*/true, 50, 18);
  Layout one = computeLayout(items, 18, true, 50, 18, /*frame=*/1, /*title=*/1);
  CHECK(d.width == one.width);
  CHECK(d.height == one.height);
  CHECK(d.title_h == one.title_h);
  CHECK(d.items[0].y == one.items[0].y);

  // margin 2 grows the title bar, the frame border and every item's y-offset.
  Layout two = computeLayout(items, 18, true, 50, 18, /*frame=*/2, /*title=*/2);
  CHECK(two.title_h == one.title_h + 2);           // titleHeight = text + 2*titleMargin
  CHECK(two.items[0].x == 2);                       // frameMargin
  CHECK(two.items[0].y == two.title_h + 2);         // title_h + frameMargin
  CHECK(two.width == one.width + 2);                // +2*(frameMargin) - 2*(1)
  CHECK(two.height == one.height + 4);              // +2 title margin, +1 top frame, +1 bottom frame
}
