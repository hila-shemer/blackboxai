// Dogfooding finding 2026-07-10: on two heads the desktop was painted only on
// the left screen - every Output parked its background buffer at layout (0,0),
// so the second head showed black plus the overhang of its own misplaced
// buffer. Each background must sit at its output's layout box, and must FOLLOW
// the box when the auto-layout reflows (head added/removed).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Output.hh"

#include <cstdlib>

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
  }
  bool bgMatchesBox(const Output *o) {
    const wlr_box bg = o->backgroundBoxForTest();
    const wlr_box fb = o->fullBox();
    return bg.x == fb.x && bg.y == fb.y && bg.width == fb.width && bg.height == fb.height;
  }
}

TEST_CASE("each head's background sits at its own layout box") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  server.addHeadlessOutputForTest(1024, 768);   // different size on purpose -
  for (int i = 0; i < 50 && server.outputCountForTest() != 2; ++i) server.dispatch();
  REQUIRE(server.outputCountForTest() == 2);    // the live bug's "bit on the right"
                                                // was the wider buffer's overhang

  const wlr_box b1 = server.outputForTest(1)->fullBox();
  REQUIRE(b1.x == 1280);                        // auto layout: second head to the right
  CHECK(bgMatchesBox(server.outputForTest(0)));
  CHECK(bgMatchesBox(server.outputForTest(1)));
}

TEST_CASE("a background follows its head when the layout reflows") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  server.addHeadlessOutputForTest(1024, 768);
  for (int i = 0; i < 50 && server.outputCountForTest() != 2; ++i) server.dispatch();
  REQUIRE(server.outputCountForTest() == 2);

  // Kill the left head: auto layout slides the survivor to x=0.
  server.destroyOutputForTest(0);
  for (int i = 0; i < 50 && server.outputCountForTest() != 1; ++i) server.dispatch();
  REQUIRE(server.outputCountForTest() == 1);
  const wlr_box fb = server.outputForTest(0)->fullBox();
  REQUIRE(fb.x == 0);
  CHECK(bgMatchesBox(server.outputForTest(0)));
}
