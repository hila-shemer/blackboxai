// B8: right-click on the desktop opens the modal root menu; outside-click and
// Escape dismiss it. A right-click on the toolbar must NOT open it.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Menu.geom.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>  // BTN_LEFT / BTN_RIGHT

using namespace bbai;

TEST_CASE("right-click desktop opens the root menu; outside/Escape dismiss") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  // Right-click on the bare desktop opens the menu.
  server.injectPointerMotionForTest(400, 300);
  server.injectPointerButtonForTest(BTN_RIGHT, /*pressed=*/true);
  CHECK(server.menuOpenForTest());

  // The menu is drawn on the overlay (a golden of the whole desktop).
  CHECK(test::compareGolden(test::captureFrame(server), "tests/golden/m4-rootmenu.png", 2, 80));

  // A left-press well outside the menu dismisses it.
  server.injectPointerMotionForTest(1000, 600);
  server.injectPointerButtonForTest(BTN_LEFT, true);
  CHECK_FALSE(server.menuOpenForTest());

  // Re-open and dismiss with Escape.
  server.injectPointerMotionForTest(400, 300);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  REQUIRE(server.menuOpenForTest());
  server.injectKeyForTest(XKB_KEY_Escape, 0, true);
  CHECK_FALSE(server.menuOpenForTest());
}

TEST_CASE("right-click on the toolbar does NOT open the root menu") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  // The toolbar bar is at {218,697,844,23}; a right-click inside it is chrome.
  server.injectPointerMotionForTest(640, 708);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  CHECK_FALSE(server.menuOpenForTest());
}

TEST_CASE("menu keyboard navigation: Down/Up skip separators, Return activates") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  server.injectPointerMotionForTest(400, 200);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  REQUIRE(server.menuOpenForTest());

  // Items: 0=foot 1=xterm 2=sep 3=Workspace1 ... Down lands on selectable rows.
  server.injectKeyForTest(XKB_KEY_Down, 0, true);
  CHECK(server.activeMenuItemForTest() == 0);          // foot
  server.injectKeyForTest(XKB_KEY_Down, 0, true);
  CHECK(server.activeMenuItemForTest() == 1);          // xterm
  server.injectKeyForTest(XKB_KEY_Down, 0, true);
  CHECK(server.activeMenuItemForTest() == 3);          // skips the separator (2)

  // Up from foot (0) wraps to the last selectable item (Exit), never a separator.
  server.injectKeyForTest(XKB_KEY_Up, 0, true);        // 3 -> 1
  server.injectKeyForTest(XKB_KEY_Up, 0, true);        // 1 -> 0
  server.injectKeyForTest(XKB_KEY_Up, 0, true);        // 0 -> wrap to Exit (last)
  const int last = server.activeMenuItemForTest();
  CHECK(last > 3);
  CHECK(server.menuOpenForTest());                     // still open

  // Navigate to a workspace row and Return -> switch + dismiss.
  while (server.menuOpenForTest() && server.activeMenuItemForTest() != 4)
    server.injectKeyForTest(XKB_KEY_Down, 0, true);
  REQUIRE(server.activeMenuItemForTest() == 4);         // Workspace 2
  server.injectKeyForTest(XKB_KEY_Return, 0, true);
  CHECK_FALSE(server.menuOpenForTest());
  CHECK(server.currentWorkspaceForTest() == 1);
}

TEST_CASE("a press inside the menu but not on an item keeps it open") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();
  REQUIRE(server.titleFont()->height() == 18);   // geometry below assumes the bundled font

  const int ox = 400, oy = 200;
  server.injectPointerMotionForTest(ox, oy);
  server.injectPointerButtonForTest(BTN_RIGHT, true);   // menu opens with top-left at (ox,oy)
  REQUIRE(server.menuOpenForTest());

  // A press on the title bar maps to no item; the menu must NOT dismiss.
  server.injectPointerMotionForTest(ox + 5, oy + 2);
  server.injectPointerButtonForTest(BTN_LEFT, true);
  CHECK(server.menuOpenForTest());

  // A press on the separator row (index 2) is also "inside but not an item".
  const int sep_y = oy + menu::titleHeight(18) + menu::kFrameMargin
                  + 2 * menu::itemHeight(18, false) + 1;
  server.injectPointerMotionForTest(ox + 30, sep_y);
  server.injectPointerButtonForTest(BTN_LEFT, true);
  CHECK(server.menuOpenForTest());
}

TEST_CASE("an arbitrary key while the menu is modal is swallowed and leaves it open") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  server.injectPointerMotionForTest(400, 200);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  REQUIRE(server.menuOpenForTest());

  server.injectKeyForTest(XKB_KEY_Down, 0, true);   // highlight item 0
  REQUIRE(server.activeMenuItemForTest() == 0);
  server.injectKeyForTest(XKB_KEY_a, 0, true);       // a non-navigation key
  CHECK(server.menuOpenForTest());                   // swallowed, not dismissed
  CHECK(server.activeMenuItemForTest() == 0);        // selection unchanged
}

TEST_CASE("the highlighted menu row renders (hilite golden)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  server.injectPointerMotionForTest(400, 300);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  REQUIRE(server.menuOpenForTest());

  server.injectKeyForTest(XKB_KEY_Down, 0, true);   // make the first row active (highlighted)
  REQUIRE(server.activeMenuItemForTest() == 0);
  CHECK(test::compareGolden(test::captureFrame(server), "tests/golden/m4-rootmenu-hilite.png", 2, 80));
}
