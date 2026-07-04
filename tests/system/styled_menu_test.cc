// tests/system/styled_menu_test.cc
// The root menu rendered under Results: bordered interlaced textures + the
// style's title/frame fonts (both resolve to the bundled font at their own
// point sizes under the isolated fontconfig).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Menu.hh"

#include <cstdlib>

using namespace bbai;

TEST_CASE("root menu under Results: styled textures + fonts") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true, "tests/fixtures/results.blackboxrc");
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  server.openRootMenu(200, 200);
  REQUIRE(server.menuOpenForTest());

  test::Frame f = test::captureFrame(server);
  auto rgb = [&](int x, int y) { return f.pixels[static_cast<size_t>(y) * f.w + x] & 0xFFFFFFu; };
  // Results menu.frame has borderWidth 1, borderColor rgb:2/2/1c: the menu's
  // top-left corner pixel is that border color.
  Menu *menu = server.rootMenuForTest();
  const int mx = menu->rectXForTest(), my = menu->rectYForTest();
  CHECK(rgb(mx, my) == 0x22221cu);

  CHECK(test::compareGolden(f, "tests/golden/v1-results-rootmenu.png", 2, 40));
}
