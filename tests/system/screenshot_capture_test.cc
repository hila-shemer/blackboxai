#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"
#include "Screenshot.hh"

#include <cstdlib>

using namespace bbai;

static void mapOne(Server &server, test::TestClient &c, int iters = 500) {
  auto mapped = [&] {
    const auto &v = server.viewsForTest();
    return !v.empty() && v[0]->isMapped();
  };
  for (int i = 0; i < iters && !mapped(); ++i) { c.flush(); server.dispatch(); c.pump(); }
  for (int i = 0; i < 30; ++i) { c.flush(); server.dispatch(); c.pump(); }
}

TEST_CASE("captureRegion crops the scene: red window inside, gradient outside") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  // A red 200x150 client; new toplevels open centered-ish (default ~160,120 incl. frame).
  test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  REQUIRE(c.ok());
  mapOne(server, c);
  View *v = server.viewsForTest()[0].get();
  const int cx = v->x() + (v->drawsFrame() ? 1 : 0);   // a point inside the client area
  const int cyp = v->y() + (v->drawsFrame() ? 23 : 0); // below the 23px titlebar

  int rw = 0, rh = 0;
  auto px = screenshot::captureRegion(server.activeSceneOutputForTest(),
                                      server.renderer,
                                      { cx + 10, cyp + 10, 40, 30 }, rw, rh);
  REQUIRE(rw == 40); REQUIRE(rh == 30);
  REQUIRE(px.size() == (size_t)rw * rh);
  // Sampled center pixel is the client red, fully opaque.
  uint32_t mid = px[(rh / 2) * rw + (rw / 2)];
  CHECK((mid >> 24) == 0xFF);                    // opaque
  CHECK((mid & 0x00FFFFFFu) == 0x00FF0000u);     // red

  // A region entirely on the desktop reads the gradient, not red.
  auto bg = screenshot::captureRegion(server.activeSceneOutputForTest(),
                                      server.renderer, { 5, 5, 20, 20 }, rw, rh);
  REQUIRE(bg.size() == (size_t)rw * rh);
  CHECK((bg[0] & 0x00FFFFFFu) != 0x00FF0000u);
}

TEST_CASE("captureRegion clamps an over-edge selection and rejects degenerate") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();
  int ow = 0, oh = 0; server.activeOutputSize(ow, oh);

  int rw = 0, rh = 0;
  auto px = screenshot::captureRegion(server.activeSceneOutputForTest(), server.renderer,
                                      { ow - 10, oh - 10, 100, 100 }, rw, rh);
  CHECK(rw == 10); CHECK(rh == 10);
  CHECK(px.size() == 100);

  auto empty = screenshot::captureRegion(server.activeSceneOutputForTest(), server.renderer,
                                         { 100, 100, 0, 0 }, rw, rh);
  CHECK(rw == 0); CHECK(rh == 0);
  CHECK(empty.empty());
}
