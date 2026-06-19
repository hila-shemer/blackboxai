// F2.5: auto-hide state + hide timer. Default auto-hide OFF (no behavior
// change). When enabled, the bar starts hidden; onPointerOverToolbar(true)
// schedules a one-shot hide_timer_ that reveals it after kHideDelayMs (250ms);
// onPointerOverToolbar(false) schedules the toggle back. Position-only test
// (no golden — auto-hide defaults OFF so m4-toolbar.png is unchanged).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Toolbar.hh"

#include <cstdlib>

using namespace bbai;

TEST_CASE("toolbar auto-hide: starts hidden, reveals on pointer-over, hides on pointer-leave") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  Toolbar *tb = server.toolbarForTest();
  REQUIRE(tb != nullptr);

  // Default: auto-hide is OFF, bar is shown.
  CHECK_FALSE(tb->hiddenForTest());

  // Enable auto-hide — bar starts hidden.
  tb->setAutoHideForTest(true);
  CHECK(tb->hiddenForTest());

  // Pointer enters the bar/sliver -> schedules reveal after kHideDelayMs (250ms).
  tb->onPointerOverToolbar(true);
  // advanceClockForTest takes seconds; 1 second >> 250ms, so the one-shot fires.
  server.advanceClockForTest(1);
  CHECK_FALSE(tb->hiddenForTest());   // revealed

  // Pointer leaves -> schedules hide after kHideDelayMs.
  tb->onPointerOverToolbar(false);
  server.advanceClockForTest(1);
  CHECK(tb->hiddenForTest());         // hidden again
}
