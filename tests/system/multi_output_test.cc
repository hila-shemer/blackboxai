// M7: every head should light up, not just the first. Boot headless (one output
// added at construction), add a second head, and assert both become Outputs
// while the toolbar stays on the primary. The existing single-output goldens are
// guarded by every other system test still capturing the first head unchanged.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"

#include <cstdlib>

using namespace bbai;

TEST_CASE("a second headless head becomes a second Output; toolbar stays primary") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.outputCountForTest() < 1; ++i) server.dispatch();
  REQUIRE(server.outputCountForTest() == 1);          // the construction-time head
  Output *primary = server.activeOutputForTest();

  server.addHeadlessOutputForTest(1280, 720);         // light up a second monitor
  for (int i = 0; i < 50 && server.outputCountForTest() < 2; ++i) server.dispatch();
  CHECK(server.outputCountForTest() == 2);

  // The first head stays primary and keeps the (single) toolbar.
  CHECK(server.activeOutputForTest() == primary);
  CHECK(server.toolbarForTest() != nullptr);
}
