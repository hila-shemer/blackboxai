// tests/system/toolbar_config_test.cc
// The three toolbar knobs land from the rc file (no ForTest setters involved):
// placement, widthPercent, autoHide - plus enableToolbar: False means no bar.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Toolbar.hh"
#include "Toolbar.geom.hh"

#include <cstdlib>

using namespace bbai;

TEST_CASE("toolbar knobs from the rc: TopCenter, 80%, auto-hidden") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true, "tests/fixtures/toolbar.blackboxrc");
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  Toolbar *tb = server.toolbarForTest();
  REQUIRE(tb != nullptr);
  CHECK(tb->placementForTest() == toolbar::Placement::TopCenter);
  CHECK(tb->hiddenForTest() == true);            // autoHide starts hidden
  const toolbar::Rect bar = tb->currentBarRect();
  CHECK(bar.w == 1024);                          // 80% of 1280
  CHECK(bar.x == 128);
  CHECK(bar.y == 0);                             // TopCenter
  CHECK(bar.h == 23);                            // builtin metrics (no styleFile in fixture)
}

TEST_CASE("enableToolbar: False boots without a toolbar") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true, "tests/fixtures/notoolbar.blackboxrc");
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();
  CHECK(server.toolbarForTest() == nullptr);
}

TEST_CASE("Results-styled toolbar golden: style metrics + textures on the bar") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true, "tests/fixtures/results-toolbar.blackboxrc");
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();
  Toolbar *tb = server.toolbarForTest();
  REQUIRE(tb != nullptr);
  // Results' toolbar metrics are formula-computed (Lucida->Liberation @10pt):
  // the bar is NOT the builtin 23 - read the live geometry, don't hardcode.
  const toolbar::Rect bar = tb->currentBarRect();
  CHECK(bar.h == server.currentStyle()->toolbarMetrics().barHeight);
  test::Frame f = test::captureFrame(server);
  CHECK(test::compareGolden(f, "tests/golden/v1-results-toolbar.png", 2, 40));
}
