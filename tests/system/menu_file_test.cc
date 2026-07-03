// M5 menu-wire: the root menu comes from a real menu file - fixture-driven
// tree + goldens, classic stat-on-open reload, [include] end-to-end, and the
// in-code fallback (empty path, missing file, pipe menuFile).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Menu.hh"
#include "Menu.geom.hh"

#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>                    // getpid - unique temp names
#include <linux/input-event-codes.h>   // BTN_LEFT / BTN_RIGHT

using namespace bbai;

namespace {
  std::string tempPath(const char *name) {
    const char *dir = getenv("TMPDIR");
    return std::string(dir ? dir : "/tmp") + "/bbai-menuwire-"
         + std::to_string(getpid()) + "-" + name;
  }

  std::string writeTemp(const char *name, const std::string &text) {
    const std::string path = tempPath(name);
    std::ofstream f(path, std::ios::trunc);
    f << text;
    return path;
  }

  void boot(Server &server) {
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
      server.dispatch();
  }
}

TEST_CASE("the root menu is built from the menu file (tree + golden + cascade)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  boot(server);
  REQUIRE(server.titleFont()->height() == 18);   // row geometry below assumes it

  const std::string path = writeTemp("golden.menu",
    "[begin] (File Menu)\n"
    "  [exec] (Terminal) {kitty}\n"
    "  [submenu] (Apps)\n"
    "    [exec] (Editor) {vi}\n"
    "  [end]\n"
    "  [workspaces] (Workspaces)\n"
    "  [config] (Configuration)\n"
    "  [restart] (Restart)\n"
    "  [exit] (Exit)\n"
    "[end]\n");
  server.setMenuFileForTest(path);

  const int ox = 400, oy = 200;
  server.injectPointerMotionForTest(ox, oy);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  REQUIRE(server.menuOpenForTest());

  Menu *m = server.rootMenuForTest();
  REQUIRE(m->itemCount() == 6);
  CHECK(m->item(0).action == MenuItem::Act::Exec);              // Terminal
  CHECK(m->item(1).kind == MenuItem::Kind::Submenu);            // Apps
  CHECK(m->item(2).kind == MenuItem::Kind::Submenu);            // Workspaces, filled live
  CHECK(m->item(2).submenu_items.size() == server.workspaces().count() + 3);
  CHECK(m->item(3).kind == MenuItem::Kind::Submenu);            // Configuration
  CHECK_FALSE(m->item(3).enabled);                              // disabled placeholder
  CHECK(m->item(4).action == MenuItem::Act::Restart);
  CHECK(m->item(5).action == MenuItem::Act::Exit);
  CHECK(test::compareGolden(test::captureFrame(server),
                            "tests/golden/m5-menufile.png", 2, 80));

  // Hover the Apps row: the parsed submenu cascades (existing Menu machinery).
  const int row_h = menu::itemHeight(18, false);
  const int apps_y = oy + menu::titleHeight(18) + menu::kFrameMargin
                   + 1 * row_h + row_h / 2;
  server.injectPointerMotionForTest(ox + 30, apps_y);
  REQUIRE(m->submenuOpenForTest());
  CHECK(m->submenuItemCountForTest() == 1);                     // Editor
  CHECK(test::compareGolden(test::captureFrame(server),
                            "tests/golden/m5-menufile-cascade.png", 2, 80));

  // The disabled Configuration row must NOT hover-open a stray empty cascade.
  const int cfg_y = oy + menu::titleHeight(18) + menu::kFrameMargin
                  + 3 * row_h + row_h / 2;
  server.injectPointerMotionForTest(ox + 30, cfg_y);
  CHECK_FALSE(m->submenuOpenForTest());
}

TEST_CASE("editing the menu file between opens rereads it (classic checkMenu)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  boot(server);

  const std::string path = writeTemp("reload.menu",
    "[begin] (v1)\n  [exec] (Alpha) {a}\n[end]\n");
  server.setMenuFileForTest(path);

  server.injectPointerMotionForTest(400, 200);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  REQUIRE(server.menuOpenForTest());
  CHECK(server.rootMenuForTest()->item(0).label == bt::decodeUtf8("Alpha"));
  server.injectKeyForTest(XKB_KEY_Escape, 0, true);
  REQUIRE_FALSE(server.menuOpenForTest());

  writeTemp("reload.menu",
    "[begin] (v2)\n  [exec] (Beta) {b}\n  [exec] (Gamma) {c}\n[end]\n");

  server.injectPointerMotionForTest(400, 200);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  REQUIRE(server.menuOpenForTest());
  REQUIRE(server.rootMenuForTest()->itemCount() == 2);   // rewrote -> reread
  CHECK(server.rootMenuForTest()->item(0).label == bt::decodeUtf8("Beta"));
}

TEST_CASE("[include] works end-to-end through the default loader") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  boot(server);

  const std::string child = writeTemp("inc-child.menu",
    "  [exec] (Two) {two}\n  [exec] (Three) {three}\n");
  const std::string parent = writeTemp("inc-parent.menu",
    "[begin] (m)\n  [exec] (One) {one}\n  [include] (" + child + ")\n[end]\n");
  server.setMenuFileForTest(parent);

  server.injectPointerMotionForTest(400, 200);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  REQUIRE(server.menuOpenForTest());
  CHECK(server.rootMenuForTest()->itemCount() == 3);

  // Editing only the INCLUDED file must also re-trigger the reload.
  server.injectKeyForTest(XKB_KEY_Escape, 0, true);
  writeTemp("inc-child.menu", "  [exec] (Two) {two}\n");
  server.injectPointerMotionForTest(400, 200);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  CHECK(server.rootMenuForTest()->itemCount() == 2);
}

TEST_CASE("missing file and pipe menuFile fall back to the in-code menu") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  boot(server);

  server.setMenuFileForTest("/nonexistent/bbai.menu");
  server.injectPointerMotionForTest(400, 200);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  REQUIRE(server.menuOpenForTest());
  CHECK(server.rootMenuForTest()->itemCount() == 7);     // the in-code menu
  CHECK(server.rootMenuForTest()->item(0).label == bt::decodeUtf8("kitty"));
  server.injectKeyForTest(XKB_KEY_Escape, 0, true);

  server.setMenuFileForTest("|genmenu");                 // pipe: locked non-goal
  server.injectPointerMotionForTest(400, 200);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  REQUIRE(server.menuOpenForTest());
  CHECK(server.rootMenuForTest()->itemCount() == 7);
}
