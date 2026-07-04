// Fullscreen geometry + classic one-saved-rect semantics. Layer coverage (the
// toolbar must vanish under a fullscreen view) is the golden test's job; here
// we pin numbers: frame fills fullBox, chrome is gone (partAt = Client
// everywhere, no titlebar), and premax restores exactly - including the classic
// maximize<->fullscreen interplay (one shared saved rect).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"
#include "Output.hh"

#include <cstdlib>

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
  }
  View *mapOne(Server &s, test::TestClient &c) {
    REQUIRE(c.ok());
    auto mapped = [&] { const auto &v = s.viewsForTest();
                        return !v.empty() && v.back()->isMapped(); };
    for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); s.dispatch(); c.pump(); }
    REQUIRE(mapped());
    for (int i = 0; i < 30; ++i) { c.flush(); s.dispatch(); c.pump(); }
    return s.viewsForTest().back().get();
  }
  void settle(Server &s, test::TestClient &c) {
    for (int i = 0; i < 30; ++i) { c.flush(); s.dispatch(); c.pump(); }
  }
}

TEST_CASE("fullscreen fills fullBox, hides chrome, restores premax on exit") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  server.focusViewForTest(v);
  const int px = v->x(), py = v->y(), pw = v->contentWidth(), ph = v->contentHeight();
  const wlr_box full = server.activeOutputForTest()->fullBox();

  server.toggleFullscreenForTest();
  settle(server, c);
  CHECK(v->isFullscreen());
  CHECK(v->x() == full.x);
  CHECK(v->y() == full.y);
  CHECK(v->contentWidth() == full.width);
  CHECK(v->contentHeight() == full.height);
  // Chrome gone: every point of the frame hit-tests to the client, and the
  // titlebar centre is NOT a Titlebar grab anymore.
  CHECK(server.partAtForTest(full.x + 5, full.y + 5) == Part::Client);

  server.toggleFullscreenForTest();
  settle(server, c);
  CHECK_FALSE(v->isFullscreen());
  CHECK(v->x() == px);
  CHECK(v->y() == py);
  CHECK(v->contentWidth() == pw);
  CHECK(v->contentHeight() == ph);
}

TEST_CASE("maximize then fullscreen then exit lands on maximized (classic reMaximize)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFF0000FFu, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  server.focusViewForTest(v);
  const int ow = v->contentWidth(), oh = v->contentHeight();
  const wlr_box work = server.activeOutputForTest()->workArea();

  v->setMaximized(true, work);
  settle(server, c);
  const int mx = v->x(), mw = v->contentWidth();

  server.toggleFullscreenForTest();            // premax NOT re-saved (already maximized)
  settle(server, c);
  CHECK(v->isFullscreen());

  server.toggleFullscreenForTest();            // exit -> reMaximize onto work area
  settle(server, c);
  CHECK(v->isMaximized());
  CHECK(v->x() == mx);
  CHECK(v->contentWidth() == mw);

  v->setMaximized(false, work);                // un-maximize -> the ORIGINAL rect
  settle(server, c);
  CHECK(v->contentWidth() == ow);
  CHECK(v->contentHeight() == oh);
}
