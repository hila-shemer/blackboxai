// Output hot-unplug hygiene. BEST-EFFORT: drives wlr_output_destroy on
// headless outputs, which no other test does - if this proves undrivable
// (wlroots-internal assert/crash, not our code), see the fallback note in
// the work-area plan: keep the defensive Server code, drop this file.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "Output.hh"
#include "View.hh"
#include "Toolbar.hh"

#include <cstdlib>

using namespace bbai;

static void settleOutputs(Server &server, int want_at_most) {
  for (int i = 0; i < 50 && server.outputCountForTest() != want_at_most; ++i)
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

TEST_CASE("second head dies: erased from tracking, maximized window re-homes") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  settleOutputs(server, 1);
  server.addHeadlessOutputForTest(1280, 720);
  settleOutputs(server, 2);
  REQUIRE(server.outputCountForTest() == 2);

  test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  REQUIRE(c.ok());
  mapOne(server, c);
  View *v = server.viewsForTest()[0].get();
  v->setPosition(1400, 100);
  v->setMaximized(true, server.outputForTest(1)->workArea());
  REQUIRE(v->x() == 1280);

  Output *primary = server.activeOutputForTest();
  server.destroyOutputForTest(1);
  settleOutputs(server, 1);
  CHECK(server.outputCountForTest() == 1);
  CHECK(server.activeOutputForTest() == primary);   // primary untouched

  // The dead head's region left the layout; the view resolved to the
  // primary fallback and got re-snapped onto a real work area.
  CHECK(v->isMaximized());
  CHECK(v->x() == 0);
  CHECK(v->y() == 0);
}

TEST_CASE("primary dies: survivor becomes primary, toolbar re-homes with its strut") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  settleOutputs(server, 1);
  server.addHeadlessOutputForTest(1280, 720);
  settleOutputs(server, 2);
  REQUIRE(server.outputCountForTest() == 2);
  Output *second = server.outputForTest(1);

  server.destroyOutputForTest(0);
  settleOutputs(server, 1);
  CHECK(server.outputCountForTest() == 1);
  REQUIRE(server.activeOutputForTest() == second);

  Toolbar *tb = server.toolbarForTest();
  REQUIRE(tb != nullptr);                       // re-homed, not dangling
  const int barH = tb->barRectForTest().h;
  const wlr_box full = second->fullBox();       // don't assume the layout
  const wlr_box w = second->workArea();         // re-normalizes x to 0
  CHECK(w.height == full.height - barH);        // the strut moved with the bar
}
