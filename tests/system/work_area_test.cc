// Work-area slice: per-output fullBox/workArea, the strut registry, and the
// toolbar registrant. Numbers only, no goldens - the slice's guarantee is
// that every EXISTING golden stays byte-identical.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Output.hh"

#include <cstdlib>

using namespace bbai;

static void settleOutputs(Server &server, int want) {
  for (int i = 0; i < 50 && server.outputCountForTest() < want; ++i)
    server.dispatch();
}

TEST_CASE("fullBox: primary at origin; add_auto puts head 2 to its right") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  settleOutputs(server, 1);
  REQUIRE(server.outputCountForTest() == 1);

  const wlr_box b0 = server.activeOutputForTest()->fullBox();
  CHECK(b0.x == 0); CHECK(b0.y == 0);
  CHECK(b0.width == 1280); CHECK(b0.height == 720);

  // THE layout pin: everything downstream (outputAt, second-head maximize)
  // assumes left-to-right auto placement. If this ever fails, stop - the
  // assumption moved, not the test.
  server.addHeadlessOutputForTest(1280, 720);
  settleOutputs(server, 2);
  REQUIRE(server.outputCountForTest() == 2);

  const wlr_box b1 = server.outputForTest(1)->fullBox();
  CHECK(b1.x == 1280); CHECK(b1.y == 0);
  CHECK(b1.width == 1280); CHECK(b1.height == 720);
}

TEST_CASE("strut registry: registrant-owned struts shrink the work area") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  settleOutputs(server, 1);
  server.addHeadlessOutputForTest(1280, 720);
  settleOutputs(server, 2);
  REQUIRE(server.outputCountForTest() == 2);

  Output *o2 = server.outputForTest(1);   // strut-free head
  const wlr_box full = o2->fullBox();
  wlr_box w = o2->workArea();
  CHECK(w.x == full.x); CHECK(w.y == full.y);
  CHECK(w.width == full.width); CHECK(w.height == full.height);

  Strut s; s.top = 50;
  o2->addStrut(&s);
  w = o2->workArea();
  CHECK(w.y == 50);
  CHECK(w.height == 670);

  s.top = 10;   // mutate in place - the registry holds the pointer
  w = o2->workArea();
  CHECK(w.y == 10);
  CHECK(w.height == 710);

  o2->removeStrut(&s);
  w = o2->workArea();
  CHECK(w.y == full.y);
  CHECK(w.height == full.height);
}
