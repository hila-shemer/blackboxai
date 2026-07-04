// Snap: half the WORK area (never the full box - the toolbar's strut stays
// reserved), un-maximizing first. Bar height is derived from the live toolbar
// rect, never baked (work-area discipline).
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

TEST_CASE("SnapLeft/Right halve the work area horizontally") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  server.focusViewForTest(v);
  const wlr_box work = server.activeOutputForTest()->workArea();

  server.snapFocusedForTest(WLR_EDGE_LEFT);
  settle(server, c);
  CHECK(v->x() == work.x);
  const int halfW = work.width / 2;
  // Frame width = content + 2*border; the frame spans the left half exactly.
  CHECK(server.frameWidthForTest(v) == halfW);
  CHECK(server.frameHeightForTest(v) == work.height);

  server.snapFocusedForTest(WLR_EDGE_RIGHT);
  settle(server, c);
  CHECK(v->x() == work.x + work.width - halfW);
}

TEST_CASE("snap un-maximizes first (no double state)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFF00FF00u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  server.focusViewForTest(v);
  const wlr_box work = server.activeOutputForTest()->workArea();
  v->setMaximized(true, work);
  settle(server, c);
  REQUIRE(v->isMaximized());

  server.snapFocusedForTest(WLR_EDGE_LEFT);
  settle(server, c);
  CHECK_FALSE(v->isMaximized());
  CHECK(server.frameWidthForTest(v) == work.width / 2);
}
