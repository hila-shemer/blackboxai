// The protocol-violation fix: a client that asks for fullscreen/maximize/
// minimize must get a configure and the state applied. Also the mpv case -
// set_fullscreen BEFORE the first commit must not assert (gotcha #13); it
// applies at map from the initial_commit re-run.
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

TEST_CASE("client set_fullscreen is honored (protocol obligation)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFFAABBCCu, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  const wlr_box full = server.activeOutputForTest()->fullBox();

  c.setFullscreen(true);
  settle(server, c);
  CHECK(v->isFullscreen());
  CHECK(v->contentWidth() == full.width);

  c.setFullscreen(false);
  settle(server, c);
  CHECK_FALSE(v->isFullscreen());
}

TEST_CASE("client set_maximized is honored") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFF445566u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  const wlr_box work = server.activeOutputForTest()->workArea();
  c.setMaximized(true);
  settle(server, c);
  CHECK(v->isMaximized());
  CHECK(v->x() == work.x);
}

TEST_CASE("pre-map set_fullscreen (mpv --fs) applies at map, no assert") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  // Ask for fullscreen the instant the toplevel exists, before the first
  // buffer commit - the handler must defer to initial_commit, not schedule a
  // configure on an uninitialized surface.
  test::TestClient c(server.socketName(), 0xFF778899u, 200, 150,
                     test::TestClient::Deco::RequestSSD,
                     /*fullscreen_before_map=*/true);
  View *v = mapOne(server, c);   // must not have asserted in schedule_configure
  settle(server, c);
  CHECK(v->isFullscreen());
}
