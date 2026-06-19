// F2.6: auto-hide hidden golden + reveal-via-pointer-motion integration test.
// Verifies that when auto-hide is enabled the bar is hidden (sliver only), that
// injectPointerMotionForTest into the bar footprint reveals it (reuses the
// existing m4-toolbar.png golden), and that moving the pointer away hides it
// again.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Toolbar.hh"
#include <linux/input-event-codes.h>

#include <cstdlib>

using namespace bbai;

TEST_CASE("toolbar auto-hide: hidden golden + reveal via pointer motion") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  Toolbar *tb = server.toolbarForTest();
  REQUIRE(tb != nullptr);

  // Enable auto-hide — bar starts hidden.
  tb->setAutoHideForTest(true);

  // HIDDEN golden: only the sliver is visible at the bottom edge.
  CHECK(tb->hiddenForTest());
  CHECK(test::compareGolden(test::captureFrame(server), "tests/golden/m4-toolbar-hidden.png", 2, 80));

  // REVEAL: move the pointer into the bar footprint (bottom-center at y~708).
  server.injectPointerMotionForTest(640, 718);
  server.advanceClockForTest(1);
  CHECK_FALSE(tb->hiddenForTest());
  CHECK(test::compareGolden(test::captureFrame(server), "tests/golden/m4-toolbar.png", 2, 80));

  // HIDE AGAIN: move the pointer away from the bar.
  server.injectPointerMotionForTest(640, 300);
  server.advanceClockForTest(1);
  CHECK(tb->hiddenForTest());
}
