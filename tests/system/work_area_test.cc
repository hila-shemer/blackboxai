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
