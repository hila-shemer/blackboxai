// Wave-2 slit: chrome lifecycle, mock-item goldens, auto-hide, click routing.
// Runs under dbus-run-session (private bus for the SNI cases) + text_env
// (captured frames include toolbar text - gotcha #20).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Output.hh"
#include "Slit.hh"
#include "SniHost.hh"
#include "SniMockItem.hh"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>
#include <unistd.h>

using namespace bbai;

static void bootOutputs(Server &server) {
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();
}

static bool pumpUntil(Server &server, std::function<bool()> done, int tries = 600) {
  for (int i = 0; i < tries && !done(); ++i) { server.dispatch(); usleep(5000); }
  return done();
}

// Write a throwaway rc; returns its path (leaks the tmpdir - fine for a test).
static std::string writeRc(const std::string &body) {
  char tmpl[] = "/tmp/bbai-slit-rc-XXXXXX";
  REQUIRE(mkdtemp(tmpl) != nullptr);
  std::string path = std::string(tmpl) + "/rc";
  std::ofstream f(path);
  f << body;
  return path;
}

TEST_CASE("slit exists on the primary; empty = invisible, zero strut") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootOutputs(server);

  Slit *sl = server.slitForTest();
  REQUIRE(sl != nullptr);
  CHECK(sl->itemCountForTest() == 0);
  CHECK(sl->placementForTest() == SlitPlacement::CenterRight);   // classic default
  CHECK(sl->directionForTest() == SlitDirection::Vertical);

  // No items -> no strut: the work area is toolbar-only, and the frame is
  // pixel-identical to the pre-slit world (zero golden churn, proven).
  const wlr_box wa = server.activeOutputForTest()->workArea();
  CHECK(wa.width == 1280);
  CHECK(test::compareGolden(test::captureFrame(server),
                            "tests/golden/m4-toolbar.png", 2, 80));
}

TEST_CASE("rc slit knobs reach the slit through applyConfig") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  const std::string rc = writeRc("session.screen0.slit.placement: TopLeft\n"
                                 "session.screen0.slit.direction: Horizontal\n");
  Server server(/*headless=*/true, rc);
  REQUIRE(server.ok());
  bootOutputs(server);
  REQUIRE(server.slitForTest() != nullptr);
  CHECK(server.slitForTest()->placementForTest() == SlitPlacement::TopLeft);
  CHECK(server.slitForTest()->directionForTest() == SlitDirection::Horizontal);
}

TEST_CASE("primary dies: slit re-homes on the survivor") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootOutputs(server);
  server.addHeadlessOutputForTest(1280, 720);
  for (int i = 0; i < 50 && server.outputCountForTest() != 2; ++i) server.dispatch();
  REQUIRE(server.outputCountForTest() == 2);
  Output *second = server.outputForTest(1);

  server.destroyOutputForTest(0);
  for (int i = 0; i < 50 && server.outputCountForTest() != 1; ++i) server.dispatch();
  REQUIRE(server.activeOutputForTest() == second);

  Slit *sl = server.slitForTest();
  REQUIRE(sl != nullptr);                          // re-homed, not dangling
  CHECK(sl->itemCountForTest() == 0);
  CHECK(second->workArea().width == second->fullBox().width);   // no ghost strut
}

TEST_CASE("slit renders a mock item: golden, strut, icon update, empty again") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootOutputs(server);
  REQUIRE(server.slitForTest() != nullptr);

  const test::Frame before = test::captureFrame(server);
  const wlr_box wa_before = server.activeOutputForTest()->workArea();

  server.createSniHostForTest();
  REQUIRE(server.sniHostForTest()->ok());
  test::SniMockChild mock;
  REQUIRE(mock.ok());
  REQUIRE(pumpUntil(server, [&] { return server.slitForTest()->itemCountForTest() == 1; }));

  // One item, CenterRight vertical: a square frame; its width left the work area.
  const slit::Rect r = server.slitForTest()->currentRect();
  CHECK(r.w == r.h);
  const wlr_box wa = server.activeOutputForTest()->workArea();
  CHECK(wa.width == wa_before.width - r.w);

  // The mock's 2x2 icon nearest-scaled to the slot: four flat quadrants incl.
  // the 0x80-alpha pixel composited over the slit texture - premultiply is
  // visibly wrong in this golden if the seam ever skips it.
  CHECK(test::compareGolden(test::captureFrame(server),
                            "tests/golden/v1-slit-one-item.png", 2, 80));

  // itemChanged repaints: switch to the second known icon.
  mock.updateIcon();
  REQUIRE(pumpUntil(server, [&] {
    const auto &items = server.sniHostForTest()->items();
    return !items.empty() && !items[0].icon_pixmaps.empty()
        && items[0].icon_pixmaps[0].data[1] == 0x11;   // kSniMockIcon2's first R byte
  }));
  CHECK(test::compareGolden(test::captureFrame(server),
                            "tests/golden/v1-slit-icon2.png", 2, 80));

  // Hard kill (no D-Bus goodbye, just the name drop): the slit empties, the
  // strut drops, and the frame returns to the pre-item pixels EXACTLY.
  mock.killHard();
  REQUIRE(pumpUntil(server, [&] { return server.slitForTest()->itemCountForTest() == 0; }));
  CHECK(server.activeOutputForTest()->workArea().width == wa_before.width);
  const test::Frame after = test::captureFrame(server);
  CHECK(after.pixels == before.pixels);
}

TEST_CASE("slit auto-hide: sliver strut, hidden golden, reveal, hide again") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  const std::string rc = writeRc("session.screen0.slit.autoHide: True\n");
  Server server(/*headless=*/true, rc);
  REQUIRE(server.ok());
  bootOutputs(server);
  server.createSniHostForTest();
  REQUIRE(server.sniHostForTest()->ok());
  test::SniMockChild mock;
  REQUIRE(mock.ok());
  REQUIRE(pumpUntil(server, [&] { return server.slitForTest()->itemCountForTest() == 1; }));

  Slit *sl = server.slitForTest();
  CHECK(sl->hidden());
  // Only the margin sliver struts while hidden (Vertical -> width shrinks).
  CHECK(server.activeOutputForTest()->workArea().width == 1280 - 2);
  CHECK(test::compareGolden(test::captureFrame(server),
                            "tests/golden/v1-slit-hidden.png", 2, 80));

  // Reveal: pointer into the SHOWN footprint (the sliver lies inside it),
  // one-shot 250ms timer fires on the virtual clock.
  const slit::Rect shown = sl->currentRect();
  server.injectPointerMotionForTest(shown.x + shown.w / 2.0, shown.y + shown.h / 2.0);
  server.advanceClockForTest(1);
  CHECK_FALSE(sl->hidden());
  // Revealed frame == the Task-4 golden: same placement, same pixels.
  CHECK(test::compareGolden(test::captureFrame(server),
                            "tests/golden/v1-slit-one-item.png", 2, 80));

  // Away -> hidden again.
  server.injectPointerMotionForTest(200, 200);
  server.advanceClockForTest(1);
  CHECK(sl->hidden());

  mock.quit();
}
