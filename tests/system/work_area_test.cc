// Work-area slice: per-output fullBox/workArea, the strut registry, and the
// toolbar registrant. Numbers only, no goldens - the slice's guarantee is
// that every EXISTING golden stays byte-identical.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Output.hh"
#include "Toolbar.hh"
#include "TestClient.hh"
#include "View.hh"

#include <cstdlib>

using namespace bbai;

static void settleOutputs(Server &server, int want) {
  for (int i = 0; i < 50 && server.outputCountForTest() < want; ++i)
    server.dispatch();
}

static void mapOne(Server &server, test::TestClient &c) {
  auto mapped = [&] {
    const auto &v = server.viewsForTest();
    return !v.empty() && v[0]->isMapped();
  };
  for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); server.dispatch(); c.pump(); }
  for (int i = 0; i < 30; ++i) { c.flush(); server.dispatch(); c.pump(); }
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

TEST_CASE("toolbar strut: primary work area = output minus the LIVE bar height") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  settleOutputs(server, 1);

  Toolbar *tb = server.toolbarForTest();
  REQUIRE(tb != nullptr);
  // The bar's own reported height - NOT toolbar::kBarHeight. When rc-style
  // makes the metric style-driven this test keeps passing.
  const int barH = tb->barRectForTest().h;

  const wlr_box w = server.activeOutputForTest()->workArea();
  CHECK(w.x == 0);
  CHECK(w.y == 0);                 // BottomCenter default: floor lifts, top stays
  CHECK(w.width == 1280);
  CHECK(w.height == 720 - barH);   // today that's 697 - the maximize_test number
}

TEST_CASE("toolbar strut follows placement and auto-hide") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  settleOutputs(server, 1);

  Toolbar *tb = server.toolbarForTest();
  REQUIRE(tb != nullptr);
  Output *primary = server.activeOutputForTest();
  const int barH = tb->barRectForTest().h;

  // Top placement: strut flips to the top edge.
  tb->setPlacement(toolbar::Placement::TopCenter);
  wlr_box w = primary->workArea();
  CHECK(w.y == barH);
  CHECK(w.height == 720 - barH);

  // Auto-hide on (still top): classic 2px sliver, not zero.
  tb->setAutoHide(true);
  w = primary->workArea();
  CHECK(w.y == toolbar::kHiddenHeight);
  CHECK(w.height == 720 - toolbar::kHiddenHeight);

  // Back to bottom with auto-hide still on: sliver moves to the floor.
  tb->setPlacement(toolbar::Placement::BottomCenter);
  w = primary->workArea();
  CHECK(w.y == 0);
  CHECK(w.height == 720 - toolbar::kHiddenHeight);

  // Auto-hide off: full bar height reserved again.
  tb->setAutoHide(false);
  w = primary->workArea();
  CHECK(w.y == 0);
  CHECK(w.height == 720 - barH);
}

TEST_CASE("outputAt/outputForView resolve heads; off-layout falls back to primary") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  settleOutputs(server, 1);
  server.addHeadlessOutputForTest(1280, 720);
  settleOutputs(server, 2);
  REQUIRE(server.outputCountForTest() == 2);

  Output *primary = server.outputForTest(0);
  Output *second  = server.outputForTest(1);
  REQUIRE(primary == server.activeOutputForTest());

  CHECK(server.outputAt(100, 100)   == primary);
  CHECK(server.outputAt(1400, 100)  == second);
  CHECK(server.outputAt(-50, -50)   == primary);   // off-layout -> active fallback

  test::TestClient c(server.socketName(), 0xFF00FF00u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  REQUIRE(c.ok());
  mapOne(server, c);
  View *v = server.viewsForTest()[0].get();

  v->setPosition(100, 100);
  CHECK(server.outputForView(v) == primary);
  v->setPosition(1400, 100);
  CHECK(server.outputForView(v) == second);
  // Straddling the seam: the frame CENTER decides. Frame is 202 wide
  // (200 + 2*border), so at x=1200 the center sits at 1301 -> second head.
  v->setPosition(1200, 100);
  CHECK(server.outputForView(v) == second);
}
