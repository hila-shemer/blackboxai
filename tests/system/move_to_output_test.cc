// Move-to-output: adjacent head in a direction, NULL past the edge (no-op).
// A plain view keeps its offset relative to the target's fullBox (clamped); a
// maximized view re-maximizes onto the target's work area.
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
    for (int i = 0; i < 40; ++i) { c.flush(); s.dispatch(); c.pump(); }
  }
}

TEST_CASE("Super+Ctrl+Right moves the frame onto the right head; edge is a no-op") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  server.addHeadlessOutputForTest(1280, 720);       // second head at x=1280 (POC layout)
  for (int i = 0; i < 50 && server.outputCountForTest() != 2; ++i) server.dispatch();
  REQUIRE(server.outputCountForTest() == 2);

  test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  server.focusViewForTest(v);
  v->setPosition(200, 200);                          // on the primary (o0)
  settle(server, c);

  server.moveFocusedToOutputForTest(WLR_DIRECTION_RIGHT);
  settle(server, c);
  const wlr_box o1 = server.outputForTest(1)->fullBox();
  CHECK(v->x() >= o1.x);
  CHECK(v->x() <  o1.x + o1.width);

  // Already on the rightmost head: RIGHT resolves to NULL -> no move.
  const int xr = v->x();
  server.moveFocusedToOutputForTest(WLR_DIRECTION_RIGHT);
  settle(server, c);
  CHECK(v->x() == xr);
}

TEST_CASE("a maximized view re-maximizes onto the target's work area") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  server.addHeadlessOutputForTest(1280, 720);
  for (int i = 0; i < 50 && server.outputCountForTest() != 2; ++i) server.dispatch();
  REQUIRE(server.outputCountForTest() == 2);

  test::TestClient c(server.socketName(), 0xFF0000FFu, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  server.focusViewForTest(v);
  v->setMaximized(true, server.outputForTest(0)->workArea());
  settle(server, c);

  server.moveFocusedToOutputForTest(WLR_DIRECTION_RIGHT);
  settle(server, c);
  const wlr_box w1 = server.outputForTest(1)->workArea();
  CHECK(v->isMaximized());
  CHECK(v->x() == w1.x);

  // Un-maximize after the move: the saved premax rect must have followed to the
  // destination head, else the restore jumps the window back to the source
  // output it was maximized on.
  const wlr_box o1full = server.outputForTest(1)->fullBox();
  v->setMaximized(false, w1);
  settle(server, c);
  CHECK_FALSE(v->isMaximized());
  CHECK(v->x() >= o1full.x);
  CHECK(v->x() <  o1full.x + o1full.width);
}
