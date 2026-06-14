// B8: menu actions — Exec runs via the injectable CommandRunner (no real
// spawn), a Workspace entry switches workspaces, and a press that would start a
// move is consumed while the menu is open (modal vs the move/resize grab).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"
#include "Menu.geom.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>  // BTN_LEFT / BTN_RIGHT

using namespace bbai;

namespace {
  // The menu opens with its top-left at the cursor; item i's centre (global).
  int itemY(int open_y, int title_h, int index) {
    return open_y + title_h + menu::kFrameMargin + index * menu::itemHeight(18, false)
         + menu::itemHeight(18, false) / 2;
  }
}

TEST_CASE("clicking the foot entry runs it via the CommandRunner (no spawn)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  FakeCommandRunner runner;
  server.setCommandRunnerForTest(&runner);

  const int ox = 400, oy = 300;
  server.injectPointerMotionForTest(ox, oy);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  REQUIRE(server.menuOpenForTest());

  // Item 0 is "foot" (title height = 20). Click its row.
  const int y = itemY(oy, /*title_h=*/menu::titleHeight(18), 0);
  server.injectPointerMotionForTest(ox + 30, y);
  server.injectPointerButtonForTest(BTN_LEFT, true);

  CHECK_FALSE(server.menuOpenForTest());           // menu dismissed on activate
  CHECK(runner.runCount() == 1);
  CHECK(runner.lastCommand() == std::vector<std::string>{"foot"});
}

TEST_CASE("clicking a workspace entry switches workspaces") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();
  CHECK(server.currentWorkspaceForTest() == 0);

  // The click coordinates below assume the isolated bundled font (height 18); a
  // font-isolation regression would otherwise silently click the wrong row.
  REQUIRE(server.titleFont()->height() == 18);

  const int ox = 400, oy = 200;
  server.injectPointerMotionForTest(ox, oy);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  REQUIRE(server.menuOpenForTest());

  // Items: 0=foot 1=xterm 2=sep 3=Workspace1 4=Workspace2 ... index 4 -> ws1.
  // Separators are kSeparatorHeight tall, so compute y by walking heights.
  int y = oy + menu::titleHeight(18) + menu::kFrameMargin;
  const int ih = menu::itemHeight(18, false), sh = menu::itemHeight(0, true);
  y += ih + ih + sh;          // skip foot, xterm, separator -> top of "Workspace 1"
  y += ih + ih / 2;            // into "Workspace 2" (index 4)
  server.injectPointerMotionForTest(ox + 30, y);
  server.injectPointerButtonForTest(BTN_LEFT, true);
  CHECK_FALSE(server.menuOpenForTest());
  CHECK(server.currentWorkspaceForTest() == 1);
}

TEST_CASE("the menu is modal: a press that would start a move is consumed") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  REQUIRE(c.ok());
  auto mapped = [&] { const auto &v = server.viewsForTest(); return !v.empty() && v[0]->isMapped(); };
  for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); server.dispatch(); c.pump(); }
  REQUIRE(mapped());
  for (int i = 0; i < 30; ++i) { c.flush(); server.dispatch(); c.pump(); }

  // Open the menu over the desktop, then press on the window's titlebar: while
  // the menu is modal that press is consumed (dismisses the menu), it does NOT
  // start an interactive move.
  server.injectPointerMotionForTest(700, 300);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  REQUIRE(server.menuOpenForTest());

  View *v = server.viewsForTest()[0].get();
  const int x0 = v->x(), y0 = v->y();
  server.injectPointerMotionForTest(260, 130);     // over the titlebar
  server.injectPointerButtonForTest(BTN_LEFT, true);
  CHECK_FALSE(server.menuOpenForTest());            // press dismissed the menu
  server.injectPointerMotionForTest(300, 160);      // would be a drag, if a grab started
  server.injectPointerButtonForTest(BTN_LEFT, false);
  CHECK(v->x() == x0);                              // window did NOT move
  CHECK(v->y() == y0);
}
