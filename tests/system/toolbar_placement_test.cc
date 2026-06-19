// F2.3: golden for the toolbar repositioned to TopCenter via the test seam.
// Bar geometry: 1280x720 output, TopCenter → x=218, y=0, w=844, h=23.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Toolbar.hh"
#include "Toolbar.geom.hh"

#include <cstdlib>

using namespace bbai;

TEST_CASE("toolbar: TopCenter placement golden") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  // Reposition via test seam — triggers rebuild() → barRect(TopCenter) → y=0.
  server.toolbarForTest()->setPlacementForTest(toolbar::Placement::TopCenter);

  const toolbar::Rect bar = toolbar::barRect(1280, 720, toolbar::Placement::TopCenter);
  CHECK(bar.x == 218);
  CHECK(bar.y == 0);
  CHECK(bar.w == 844);
  CHECK(bar.h == 23);

  test::Frame f = test::captureFrame(server);
  REQUIRE(f.w == 1280u);
  REQUIRE(f.h == 720u);

  auto rgb    = [&](int x, int y) { return f.pixels[static_cast<size_t>(y) * f.w + x] & 0x00FFFFFFu; };
  auto isGrey = [&](uint32_t c) {
    return ((c >> 16) & 0xFF) == ((c >> 8) & 0xFF) && ((c >> 8) & 0xFF) == (c & 0xFF);
  };

  // A pixel well inside the top bar band is grey chrome.
  CHECK(isGrey(rgb(640, 10)));
  // A pixel just below the bar (y=30) is the desktop gradient — not grey.
  CHECK_FALSE(isGrey(rgb(640, 30)));

  CHECK(test::compareGolden(f, "tests/golden/m4-toolbar-topcenter.png", 2, 80));
}
