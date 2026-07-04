// The parked cursor-axis defect: scroll must reach a focused client, be
// discarded under a lock (but still reset idle timers), and - the synthesis-
// flagged case - ride the implicit grab so a mid-drag scroll goes to the
// grabbed surface no matter where the cursor wandered.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
  }
  // Map one SSD client at the default (160,120), 200x150; return it mapped.
  void mapClient(Server &s, test::TestClient &c) {
    REQUIRE(c.ok());
    auto mapped = [&] { const auto &v = s.viewsForTest();
                        return !v.empty() && v.back()->isMapped(); };
    for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); s.dispatch(); c.pump(); }
    REQUIRE(mapped());
    for (int i = 0; i < 30; ++i) { c.flush(); s.dispatch(); c.pump(); }
  }
  void pumpAxis(Server &s, test::TestClient &c) {
    // Vertical scroll up = one notch; delivery is to the seat's focused surface.
    s.injectPointerAxisForTest(WL_POINTER_AXIS_VERTICAL_SCROLL, -15.0, -120);
    for (int i = 0; i < 20; ++i) { c.flush(); s.dispatch(); c.pump(); }
  }
}

TEST_CASE("axis over a focused client reaches it; over the bare desktop does not") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  // ClickToFocus so a stray hover doesn't refocus - this test is about axis,
  // not focus policy (sloppy focus is exercised in its own test).
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  mapClient(server, c);

  // Cursor over the client area (client x[161,360) y[143,293)); focus + pointer.
  server.injectPointerMotionForTest(250, 200);
  for (int i = 0; i < 10; ++i) { c.flush(); server.dispatch(); c.pump(); }
  const int before = c.pointerAxisEvents();
  pumpAxis(server, c);
  CHECK(c.pointerAxisEvents() == before + 1);

  // Cursor over the bare desktop (top-left corner): no client has pointer focus,
  // and (default True) the desktop wheel gate consumes it - either way the
  // client sees nothing new. (The workspace-switch effect is Task 4's test.)
  server.injectPointerMotionForTest(5, 5);
  const int held = c.pointerAxisEvents();
  pumpAxis(server, c);
  CHECK(c.pointerAxisEvents() == held);
}

TEST_CASE("axis while locked is discarded but still reset idle activity") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFF00FF00u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  mapClient(server, c);
  server.injectPointerMotionForTest(250, 200);
  for (int i = 0; i < 10; ++i) { c.flush(); server.dispatch(); c.pump(); }

  server.lockForTest();                 // wave-1 lock hook (see note below)
  const int before = c.pointerAxisEvents();
  server.injectPointerAxisForTest(WL_POINTER_AXIS_VERTICAL_SCROLL, -15.0, -120);
  for (int i = 0; i < 20; ++i) { c.flush(); server.dispatch(); c.pump(); }
  CHECK(c.pointerAxisEvents() == before);         // no client sees axis under a lock
  CHECK(server.idleActivityCountForTest() > 0);   // but idle timers were reset
}

TEST_CASE("mid-drag axis rides the implicit grab (held on A, cursor over desktop -> A)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  mapClient(server, a);

  // Press a button inside A's client area (implicit grab; button_count > 0).
  server.injectPointerMotionForTest(250, 200);
  server.injectPointerButtonForTest(BTN_LEFT, true);
  for (int i = 0; i < 10; ++i) { a.flush(); server.dispatch(); a.pump(); }

  // Wander over the bare desktop and scroll: the grab keeps delivery on A, and
  // the wheel-region gate must be BYPASSED (no workspace switch).
  const unsigned ws0 = server.currentWorkspaceForTest();
  server.injectPointerMotionForTest(5, 5);
  const int before = a.pointerAxisEvents();
  server.injectPointerAxisForTest(WL_POINTER_AXIS_VERTICAL_SCROLL, -15.0, -120);
  for (int i = 0; i < 20; ++i) { a.flush(); server.dispatch(); a.pump(); }
  CHECK(a.pointerAxisEvents() == before + 1);          // A got the scroll
  CHECK(server.currentWorkspaceForTest() == ws0);      // desktop gate skipped
  server.injectPointerButtonForTest(BTN_LEFT, false);
}
