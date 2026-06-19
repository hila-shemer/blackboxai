// Demo scenario: toolbar_autohide
// Enables auto-hide, shows the 2px sliver, moves the pointer over the bar to
// reveal it, then moves away to hide it again.
// Asserts toolbar hidden state at each step; dumps frames when BBAI_DEMO_DIR set.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "DemoRecorder.hh"
#include "Server.hh"
#include "Toolbar.hh"

#include <cstdlib>

using namespace bbai;

// Headless output is 1280x720; default toolbar is BottomCenter at y=697.
// The sliver sits at y=718 (720-2). Moving to y=718 is over the toolbar area.
static constexpr int kToolbarY = 718;

TEST_CASE("demo: toolbar_autohide — sliver, reveal on pointer-over, hide on pointer-leave") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  test::DemoRecorder rec("toolbar_autohide");

  Toolbar *tb = server.toolbarForTest();
  REQUIRE(tb != nullptr);

  // Enable auto-hide — bar starts hidden (2px sliver).
  tb->setAutoHideForTest(true);
  CHECK(tb->hiddenForTest());
  rec.shot(server, 2);

  // --- Move pointer onto the toolbar sliver to trigger reveal ---
  server.injectPointerMotionForTest(640, kToolbarY);
  tb->onPointerOverToolbar(true);
  server.advanceClockForTest(1);

  CHECK_FALSE(tb->hiddenForTest());
  rec.shot(server, 3);

  // --- Move pointer away to trigger hide ---
  server.injectPointerMotionForTest(640, 300);
  tb->onPointerOverToolbar(false);
  server.advanceClockForTest(1);

  CHECK(tb->hiddenForTest());
  rec.shot(server, 3);
}
