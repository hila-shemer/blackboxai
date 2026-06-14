// B8: right-click on the desktop opens the modal root menu; outside-click and
// Escape dismiss it. A right-click on the toolbar must NOT open it.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"

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
