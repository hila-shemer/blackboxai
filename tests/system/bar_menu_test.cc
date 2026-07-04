// Toolbar/Slit right-click gestures: open the titled menu on the chrome, click
// a knob -> config_ flips + applyConfig re-applies it live + the classic rc key
// is written. rc contents checked after updateRcKey (tmp rc fixture). Slit menu
// opens on a right-click over the slit FRAME that misses an icon (an icon's own
// right-click is the item's SNI context menu, slit's job). dbus-run-session +
// text_env: the slit needs a bus (its items come from a Host) and the frames
// carry toolbar text.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Toolbar.hh"
#include "Slit.hh"
#include "SniHost.hh"
#include "SniMockItem.hh"
#include "Menu.hh"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <unistd.h>
#include <linux/input-event-codes.h>

using namespace bbai;

namespace {
  void boot(Server &server) {
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
      server.dispatch();
    REQUIRE(server.titleFont()->height() == 18);
  }
  bool pumpUntil(Server &s, std::function<bool()> done, int tries = 600) {
    for (int i = 0; i < tries && !done(); ++i) { s.dispatch(); usleep(5000); }
    return done();
  }
  std::string slurp(const std::string &p) {
    std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); return ss.str();
  }
  std::string writeRc(const std::string &body) {
    char tmpl[] = "/tmp/bbai-barmenu-XXXXXX";
    REQUIRE(mkdtemp(tmpl) != nullptr);
    std::string path = std::string(tmpl) + "/rc";
    std::ofstream f(path); f << body; return path;
  }
  int rowX(const Menu *m) { return m->rectXForTest() + 5; }
  int rowCenterY(const Menu *m, int idx) {
    int top = -1, bot = -1;
    for (int y = m->rectYForTest(); y < m->rectYForTest() + 800; ++y) {
      if (m->itemIndexAtGlobal(rowX(m), y) == idx) { if (top < 0) top = y; bot = y; }
      else if (top >= 0) break;
    }
    REQUIRE(top >= 0);
    return (top + bot) / 2;
  }
  void hover(Server &s, const Menu *m, int idx) {
    s.injectPointerMotionForTest(rowX(m), rowCenterY(m, idx));
  }
  void click(Server &s, const Menu *m, int idx) {
    hover(s, m, idx); s.injectPointerButtonForTest(BTN_LEFT, true);
  }
}

TEST_CASE("toolbar right-click: Auto Hide toggles live and persists the classic key") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  const std::string rc = writeRc("");             // empty rc -> defaults, we write into it
  Server server(/*headless=*/true, rc);
  boot(server);
  REQUIRE(server.toolbarForTest() != nullptr);
  CHECK_FALSE(server.toolbarForTest()->hiddenForTest());

  const toolbar::Rect br = server.toolbarForTest()->barRectForTest();
  server.injectPointerMotionForTest(br.x + br.w / 2, br.y + br.h / 2);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  REQUIRE(server.menuOpenForTest());
  Menu *m = server.rootMenuForTest();             // rows: 0 Enable, 1 sep, 2 Placement, 3 AutoHide
  REQUIRE(m->itemCount() == 4);

  click(server, m, 3);                            // Auto Hide
  CHECK_FALSE(server.menuOpenForTest());
  CHECK(server.config().toolbar.autoHide);        // in-memory flip
  CHECK(server.toolbarForTest()->hiddenForTest()); // applyConfig re-applied it live
  CHECK(slurp(rc).find("session.screen0.toolbar.autoHide: True\n") != std::string::npos);
}

TEST_CASE("toolbar right-click: Placement submenu moves the bar and persists") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  const std::string rc = writeRc("");
  Server server(/*headless=*/true, rc);
  boot(server);
  const toolbar::Rect br = server.toolbarForTest()->barRectForTest();
  server.injectPointerMotionForTest(br.x + br.w / 2, br.y + br.h / 2);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  Menu *m = server.rootMenuForTest();
  hover(server, m, 2);                            // Placement cascade
  REQUIRE(m->submenuOpenForTest());
  Menu *pl = m->child();
  click(server, pl, 0);                           // Top Left
  CHECK(server.config().toolbar.placement == toolbar::Placement::TopLeft);
  CHECK(server.toolbarForTest()->placementForTest() == toolbar::Placement::TopLeft);
  CHECK(slurp(rc).find("session.screen0.toolbar.placement: TopLeft\n") != std::string::npos);
}

TEST_CASE("slit frame right-click opens the Slit menu; Direction flips config + rc") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  const std::string rc = writeRc("");
  Server server(/*headless=*/true, rc);
  boot(server);
  server.createSniHostForTest();
  REQUIRE(server.sniHostForTest()->ok());
  test::SniMockChild mock;                        // default: no dbusmenu server, just an icon
  REQUIRE(mock.ok());
  REQUIRE(pumpUntil(server, [&]{ return server.slitForTest()->itemCountForTest() == 1; }));

  // Right-click the slit FRAME border (not the icon cell): the top-left corner
  // of the frame is margin+border, always outside the first cell.
  const slit::Rect fr = server.slitForTest()->currentRect();
  server.injectPointerMotionForTest(fr.x + 1, fr.y + 1);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  REQUIRE(server.menuOpenForTest());
  Menu *m = server.rootMenuForTest();             // rows: 0 Direction, 1 Placement, 2 AutoHide
  REQUIRE(m->itemCount() == 3);
  hover(server, m, 0);                            // Direction cascade
  REQUIRE(m->submenuOpenForTest());
  Menu *dir = m->child();
  click(server, dir, 0);                          // Horizontal
  CHECK(server.config().slit.direction == SlitDirection::Horizontal);
  CHECK(server.slitForTest()->directionForTest() == SlitDirection::Horizontal);
  CHECK(slurp(rc).find("session.screen0.slit.direction: Horizontal\n") != std::string::npos);
  mock.quit();
}
