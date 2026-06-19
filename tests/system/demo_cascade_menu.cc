// Demo scenario: cascade_menu
// Right-clicks the desktop to open the root menu, hovers into the Workspaces
// submenu row, hovers a child row, then dismisses with Escape.
// Asserts menu/submenu state at each step; dumps frames when BBAI_DEMO_DIR set.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "DemoRecorder.hh"
#include "Server.hh"
#include "Menu.hh"
#include "Menu.geom.hh"
#include "Workspace.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>

using namespace bbai;

namespace {
  // Centre Y of item `index` in the root menu opened at (ox, oy).
  // Root-menu separator is at index 2; all others are normal height.
  int itemCentreY(int open_y, int index) {
    const int title_h = menu::titleHeight(18);
    int y = open_y + title_h + menu::kFrameMargin;
    for (int i = 0; i <= index; ++i) {
      const bool sep = (i == 2);
      const int h = menu::itemHeight(18, sep);
      if (i == index) return y + h / 2;
      y += h;
    }
    return y;
  }

  // Centre Y of child-menu item `ci`, given the live child pointer.
  int childItemCentreY(const Menu *child, int ci) {
    WorkspaceModel ws;
    const int title_h = menu::titleHeight(18);
    int y = child->rectYForTest() + title_h + menu::kFrameMargin;
    for (int i = 0; i <= ci; ++i) {
      const bool sep = (static_cast<unsigned>(i) == ws.count());
      const int h = menu::itemHeight(18, sep);
      if (i == ci) return y + h / 2;
      y += h;
    }
    return y;
  }
}

TEST_CASE("demo: cascade_menu — right-click desktop, hover submenu, dismiss with Escape") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  test::DemoRecorder rec("cascade_menu");

  const int ox = 400, oy = 200;

  // --- Right-click the desktop to open the root menu ---
  server.injectPointerMotionForTest(ox, oy);
  server.injectPointerButtonForTest(BTN_RIGHT, /*pressed=*/true);

  CHECK(server.menuOpenForTest());
  rec.shot(server);

  // --- Hover the "Workspaces" row (index 3) to open the submenu ---
  // Root menu: 0=foot, 1=xterm, 2=sep, 3=Workspaces, 4=sep, 5=Restart, 6=Exit
  const int ws_y = itemCentreY(oy, 3);
  server.injectPointerMotionForTest(ox + 30, ws_y);

  Menu *root = server.rootMenuForTest();
  REQUIRE(root != nullptr);
  CHECK(root->submenuOpenForTest());
  rec.shot(server, 3);

  // --- Hover a child workspace row (child item 0 = Workspace 1) ---
  Menu *child = root->child();
  REQUIRE(child != nullptr);
  const int cy = childItemCentreY(child, 0);
  const int cx = child->rectXForTest() + 30;
  server.injectPointerMotionForTest(cx, cy);
  rec.shot(server, 2);

  // --- Press Escape to dismiss the whole chain ---
  server.injectKeyForTest(XKB_KEY_Escape, 0, /*pressed=*/true);

  CHECK_FALSE(server.menuOpenForTest());
  rec.shot(server);
}
