// The Windowmenu end-to-end: right-click a titlebar -> the no-title menu opens;
// clicking a row drives the real iconify/maximize/close/send-to-workspace on the
// View. Click coordinates come from the menu's own hit-test accessors, never
// baked margin math. text_env pins the font (gotcha #20).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"
#include "Menu.hh"
#include "Output.hh"

#include <linux/input-event-codes.h>

using namespace bbai;

namespace {
  void boot(Server &server) {
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
      server.dispatch();
    REQUIRE(server.titleFont()->height() == 18);
  }
  View *mapOne(Server &server, test::TestClient &c) {
    auto mapped = [&] {
      const auto &v = server.viewsForTest();
      return !v.empty() && v.back()->isMapped();
    };
    for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); server.dispatch(); c.pump(); }
    REQUIRE(mapped());
    for (int i = 0; i < 30; ++i) { c.flush(); server.dispatch(); c.pump(); }
    return server.viewsForTest().back().get();
  }
  int rowX(const Menu *m) { return m->rectXForTest() + 5; }
  int rowCenterY(const Menu *m, int idx) {
    int top = -1, bot = -1;
    for (int y = m->rectYForTest(); y < m->rectYForTest() + 600; ++y) {
      if (m->itemIndexAtGlobal(rowX(m), y) == idx) { if (top < 0) top = y; bot = y; }
      else if (top >= 0) break;
    }
    REQUIRE(top >= 0);
    return (top + bot) / 2;
  }
  void clickRow(Server &server, const Menu *m, int idx) {
    server.injectPointerMotionForTest(rowX(m), rowCenterY(m, idx));
    server.injectPointerButtonForTest(BTN_LEFT, true);
  }
  // Right-click the view's titlebar to open its Windowmenu. The frame's title
  // bar sits at the top of the frame; a few px in is safely on it.
  Menu *openWindowMenu(Server &server, View *v) {
    // Click the label centre of the titlebar - x=20 lands on the iconify button
    // (its rect spans fx 2..21), which is not a Titlebar/Label part.
    server.injectPointerMotionForTest(v->x() + 100, v->y() + 3);
    server.injectPointerButtonForTest(BTN_RIGHT, true);
    REQUIRE(server.menuOpenForTest());
    return server.rootMenuForTest();
  }
}

TEST_CASE("titlebar right-click opens the Windowmenu; Iconify hides the window") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFF3366CCu, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  REQUIRE(c.ok());
  View *v = mapOne(server, c);
  v->setPosition(300, 200);

  Menu *m = openWindowMenu(server, v);
  REQUIRE(m->itemCount() == 6);
  CHECK_FALSE(v->isIconified());
  clickRow(server, m, 2);                          // Iconify
  CHECK_FALSE(server.menuOpenForTest());           // toggle closes the chain
  CHECK(v->isIconified());
}

TEST_CASE("Maximize toggles; a second open shows it checked") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFF33CC66u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  REQUIRE(c.ok());
  View *v = mapOne(server, c);
  v->setPosition(250, 180);

  Menu *m = openWindowMenu(server, v);
  CHECK_FALSE(m->item(3).checked);
  clickRow(server, m, 3);                           // Maximize
  CHECK(v->isMaximized());
  m = openWindowMenu(server, v);
  CHECK(m->item(3).checked);                         // fresh mark on reopen
  server.injectKeyForTest(XKB_KEY_Escape, 0, true);
}

TEST_CASE("Send To... re-homes the window and repairs focus") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  REQUIRE(server.workspaces().count() >= 2u);
  test::TestClient c(server.socketName(), 0xFFCC6633u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  REQUIRE(c.ok());
  View *v = mapOne(server, c);
  v->setPosition(280, 220);
  REQUIRE(v->workspace() == 0u);
  CHECK(server.focusedViewForTest() == v);

  Menu *m = openWindowMenu(server, v);
  server.injectPointerMotionForTest(rowX(m), rowCenterY(m, 0));   // hover Send To...
  REQUIRE(m->submenuOpenForTest());
  Menu *sub = m->child();
  clickRow(server, sub, 1);                          // send to workspace 1
  CHECK(v->workspace() == 1u);
  CHECK_FALSE(v->visible());                          // ws1 not current -> hidden
  CHECK(server.focusedViewForTest() != v);            // focus left the hidden window
}

TEST_CASE("Close asks the client to close") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFF6633CCu, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  REQUIRE(c.ok());
  View *v = mapOne(server, c);
  v->setPosition(260, 200);
  Menu *m = openWindowMenu(server, v);
  clickRow(server, m, 5);                             // Close
  CHECK_FALSE(server.menuOpenForTest());
  bool got_close = false;
  for (int i = 0; i < 200 && !got_close; ++i) {
    c.flush(); server.dispatch(); c.pump();
    got_close = c.gotCloseRequest();
  }
  CHECK(got_close);
}

TEST_CASE("golden: the no-title Windowmenu renders (builtin style)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFF3366CCu, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  REQUIRE(c.ok());
  View *v = mapOne(server, c);
  v->setPosition(300, 200);
  Menu *m = openWindowMenu(server, v);
  CHECK(test::compareGolden(test::captureFrame(server),
                            "tests/golden/v1-windowmenu.png", 2, 40));
  server.injectKeyForTest(XKB_KEY_Escape, 0, true);
}
