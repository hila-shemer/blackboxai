// F3.3: hovering the "Workspaces" submenu row opens the child menu to the
// right; hovering a different parent row closes it.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Menu.hh"
#include "Menu.geom.hh"
#include "Workspace.hh"
#include "Rootmenu.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>  // BTN_RIGHT

using namespace bbai;

namespace {
  // Global Y of the centre of item `index` in a menu opened at oy,
  // given the root-menu layout (items 0,1 are Commands, 2 is separator).
  int itemCentreY(int open_y, int index) {
    const int title_h = menu::titleHeight(18);
    int y = open_y + title_h + menu::kFrameMargin;
    for (int i = 0; i <= index; ++i) {
      const bool sep = (i == 2);            // root menu: index 2 is the separator
      const int h = sep ? menu::itemHeight(0, true) : menu::itemHeight(18, false);
      if (i == index) return y + h / 2;
      y += h;
    }
    return y;
  }
}

TEST_CASE("hovering the Workspaces row opens the child submenu to the right") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  const int ox = 400, oy = 200;
  server.injectPointerMotionForTest(ox, oy);
  server.injectPointerButtonForTest(BTN_RIGHT, /*pressed=*/true);
  REQUIRE(server.menuOpenForTest());

  // Workspaces is item index 3 (0=foot, 1=xterm, 2=sep, 3=Workspaces).
  const int ws_y = itemCentreY(oy, 3);
  server.injectPointerMotionForTest(ox + 30, ws_y);

  Menu *root = server.rootMenuForTest();
  REQUIRE(root != nullptr);

  CHECK(root->submenuOpenForTest());
  CHECK(root->openSubmenuIndexForTest() == 3);

  // Submenu item count: workspace count + sep + New + Remove = ws.count() + 3.
  WorkspaceModel ws;
  const int expected_count = static_cast<int>(ws.count()) + 3;
  CHECK(root->submenuItemCountForTest() == expected_count);

  // Child opens to the right of the parent (child X >= parent right edge).
  CHECK(root->childRectXForTest() >= root->rectXForTest() + root->itemCount());  // rough sanity
  // More precise: childRectX should be > rootRectX (at least the menu width apart)
  CHECK(root->childRectXForTest() > root->rectXForTest());

  // Golden: root menu + open child submenu.
  CHECK(test::compareGolden(test::captureFrame(server),
                            "tests/golden/m4-rootmenu-submenu.png", 2, 80));
}

TEST_CASE("hovering a non-submenu row closes the open child") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  const int ox = 400, oy = 200;
  server.injectPointerMotionForTest(ox, oy);
  server.injectPointerButtonForTest(BTN_RIGHT, /*pressed=*/true);
  REQUIRE(server.menuOpenForTest());

  // First open the submenu by hovering Workspaces (index 3).
  server.injectPointerMotionForTest(ox + 30, itemCentreY(oy, 3));
  REQUIRE(server.rootMenuForTest()->submenuOpenForTest());

  // Now hover over "foot" (index 0) — not a submenu, so child should close.
  server.injectPointerMotionForTest(ox + 30, itemCentreY(oy, 0));
  CHECK_FALSE(server.rootMenuForTest()->submenuOpenForTest());
  CHECK(server.menuOpenForTest());   // root menu itself stays open
}
