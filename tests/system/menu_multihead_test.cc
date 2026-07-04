// The parked defect: Menu::show clamped against the PRIMARY output's size at
// implicit origin (0,0), so a menu opened on a second head (auto-laid at
// x=1280) snapped back onto head 1. Fix clamps against outputAt(pt)->fullBox()
// (layout coords). Cascade children re-clamp per-child through the same show().
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Output.hh"
#include "Menu.hh"

#include <linux/input-event-codes.h>

using namespace bbai;

namespace {
  void boot(Server &server) {
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
      server.dispatch();
    REQUIRE(server.titleFont()->height() == 18);   // gotcha #20
  }
  void twoHeads(Server &server) {
    server.addHeadlessOutputForTest(1280, 720);
    for (int i = 0; i < 50 && server.outputCountForTest() != 2; ++i) server.dispatch();
    REQUIRE(server.outputCountForTest() == 2);
  }
}

TEST_CASE("root menu stays on the second head instead of snapping to the primary") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  twoHeads(server);
  server.setMenuFileForTest("");                   // the in-code menu (has a submenu)

  // Open well onto head 2 (layout x in [1280, 2560)). Pre-fix this clamped to
  // < 1280 (head 1); post-fix it lands on head 2.
  server.openRootMenu(1500, 100);
  REQUIRE(server.menuOpenForTest());
  Menu *root = server.rootMenuForTest();
  CHECK(root->rectXForTest() >= 1280);
  CHECK(root->rectXForTest() == 1500);             // fits: not clamped at all
}

TEST_CASE("a cascade submenu clamps within the head it opened on") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  twoHeads(server);
  server.setMenuFileForTest("");

  // Open near head 2's right edge so the Workspaces cascade would overflow
  // 2560 and must clamp back - onto head 2, never head 1.
  server.openRootMenu(2400, 100);
  REQUIRE(server.menuOpenForTest());
  Menu *root = server.rootMenuForTest();
  // in-code rows: 0 kitty, 1 xterm, 2 sep, 3 Workspaces
  root->openSubmenuAt(3);
  REQUIRE(root->submenuOpenForTest());
  CHECK(root->childRectXForTest() >= 1280);        // on head 2, not snapped to head 1
  CHECK(root->childRectXForTest() < 2560);         // and clamped inside head 2
}
