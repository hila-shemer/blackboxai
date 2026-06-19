// A7: the toolbar renders bottom-center on the top layer with the current
// workspace name, a (blank) window label, four arrow buttons, and a ticking
// clock driven by the injectable VirtualClock — no wall clock, two goldens.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Toolbar.hh"
#include "Toolbar.geom.hh"
#include "Clock.hh"

#include <cstdlib>

using namespace bbai;

TEST_CASE("toolbar: bottom-center bar, workspace name, ticking clock") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  const toolbar::Rect bar = toolbar::barRect(1280, 720);
  CHECK(bar.x == 218);
  CHECK(bar.y == 697);
  CHECK(bar.w == 844);
  CHECK(bar.h == 23);

  // F2.2: Toolbar carries a placement member; default is BottomCenter.
  CHECK(server.toolbarForTest()->placementForTest() == toolbar::Placement::BottomCenter);
  CHECK(server.toolbarForTest()->barRectForTest().y == 697);

  test::Frame f = test::captureFrame(server);
  REQUIRE(f.w == 1280u);
  REQUIRE(f.h == 720u);
  auto rgb    = [&](int x, int y) { return f.pixels[static_cast<size_t>(y) * f.w + x] & 0x00FFFFFFu; };
  auto isGrey = [&](uint32_t c) {
    return ((c >> 16) & 0xFF) == ((c >> 8) & 0xFF) && ((c >> 8) & 0xFF) == (c & 0xFF);
  };

  // A pixel inside the bar is grey chrome; just above it is the gradient desktop.
  CHECK(isGrey(rgb(640, 708)));
  CHECK_FALSE(isGrey(rgb(640, 690)));
  // The label region has rendered dark glyph pixels ("Workspace 1").
  int dark = 0;
  for (int y = 700; y < 716; ++y)
    for (int x = 221; x < 320; ++x) {
      uint32_t c = rgb(x, y);
      if (int((c >> 16) & 0xFF) + int((c >> 8) & 0xFF) + int(c & 0xFF) < 150) ++dark;
    }
  CHECK(dark > 5);

  CHECK(bt::formatClock(server.wallSecondsForTest()) == "02:05 PM");
  CHECK(test::compareGolden(f, "tests/golden/m4-toolbar.png", 2, 40));

  // Advance one minute deterministically -> the clock cell re-renders to 02:06 PM.
  server.advanceClockForTest(60);
  CHECK(bt::formatClock(server.wallSecondsForTest()) == "02:06 PM");
  test::Frame f2 = test::captureFrame(server);
  CHECK(test::compareGolden(f2, "tests/golden/m4-toolbar-tick.png", 2, 40));
}
