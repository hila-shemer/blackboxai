// Locking mid-modal-mode must abort the mode (menu, screenshot select, alt-tab
// cycle, move/resize grab) - otherwise the mode's exit path re-syncs seat
// state into a locked session. One test per mode; each also proves the mode
// machinery still works after unlock.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "LockTestClient.hh"
#include "Server.hh"
#include "SessionLock.hh"
#include "View.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>

using namespace bbai;

namespace {

  void bootOutput(Server &server) {
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
      server.dispatch();
    REQUIRE(server.activeSceneOutputForTest() != nullptr);
  }

  template <typename Cond, typename Pump>
  bool pumpUntil(Server &server, Cond cond, Pump pump, int iters = 1000) {
    for (int i = 0; i < iters && !cond(); ++i) {
      pump();
      server.dispatch();
      pump();
    }
    return cond();
  }

  // Lock via the fallback path (deterministic, no real-time wait).
  template <typename Pump>
  void lockNow(Server &server, test::LockTestClient &lc, Pump pump) {
    REQUIRE(pumpUntil(server, [&] { return lc.sawLockManager(); }, pump));
    lc.lock();
    REQUIRE(pumpUntil(server,
        [&] { return server.sessionLockForTest()->locked(); }, pump));
    server.advanceClockForTest(2);
    REQUIRE(pumpUntil(server, [&] { return lc.lockedReceived(); }, pump));
  }

  void mapOne(Server &server, test::TestClient &c) {
    auto mapped = [&] {
      const auto &v = server.viewsForTest();
      return !v.empty() && v.back()->isMapped();
    };
    REQUIRE(pumpUntil(server, mapped, [&] { c.flush(); c.pump(); }));
    for (int i = 0; i < 40; ++i) { c.flush(); server.dispatch(); c.pump(); }
  }

} // namespace

TEST_CASE("locking closes an open root menu") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);
    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    server.openRootMenu(200, 200);
    REQUIRE(server.menuOpenForTest());

    test::LockTestClient lc(server.socketName());
    REQUIRE(lc.ok());
    lockNow(server, lc, [&] { lc.flush(); lc.pump(); });
    CHECK_FALSE(server.menuOpenForTest());

    // Menu keys are dead while locked (would crash/act on a live menu chain).
    server.injectKeyForTest(XKB_KEY_Down, 0, true);
    CHECK_FALSE(server.menuOpenForTest());

    lc.unlockAndDestroy();
    REQUIRE(pumpUntil(server, [&] { return !server.sessionLockForTest()->locked(); },
                      [&] { lc.flush(); lc.pump(); }));
    server.openRootMenu(200, 200);   // machinery intact after the round-trip
    CHECK(server.menuOpenForTest());
    server.closeMenus();
}

TEST_CASE("locking cancels screenshot select, mid-drag overlay included") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);
    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    server.injectKeyForTest(XKB_KEY_F7, WLR_MODIFIER_LOGO, true);   // arm
    REQUIRE(server.screenshotActiveForTest());
    server.injectPointerMotionForTest(100, 100);
    server.injectPointerButtonForTest(BTN_LEFT, true);              // drag started
    server.injectPointerMotionForTest(300, 260);
    REQUIRE(server.screenshotOverlayActiveForTest());

    test::LockTestClient lc(server.socketName());
    REQUIRE(lc.ok());
    lockNow(server, lc, [&] { lc.flush(); lc.pump(); });
    CHECK_FALSE(server.screenshotActiveForTest());
    CHECK_FALSE(server.screenshotOverlayActiveForTest());

    // The dangling release must not capture anything: gate swallows it.
    server.injectPointerButtonForTest(BTN_LEFT, false);
    CHECK(server.seatSelectionSourceForTest() == nullptr);
}

TEST_CASE("locking dissolves an alt-tab cycle session") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);
    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150);
    REQUIRE(a.ok());
    mapOne(server, a);
    test::TestClient b(server.socketName(), 0xFF00FF00u, 200, 150);
    REQUIRE(b.ok());
    mapOne(server, b);

    server.cycleForTest(+1);
    REQUIRE(server.cyclingForTest());

    test::LockTestClient lc(server.socketName());
    REQUIRE(lc.ok());
    auto pump = [&] { a.flush(); a.pump(); b.flush(); b.pump(); lc.flush(); lc.pump(); };
    lockNow(server, lc, pump);
    CHECK_FALSE(server.cyclingForTest());
    CHECK(server.focusedViewForTest() == nullptr);   // lock parked focus after the cancel

    lc.unlockAndDestroy();
    REQUIRE(pumpUntil(server, [&] { return !server.sessionLockForTest()->locked(); }, pump));
    CHECK(server.focusedViewForTest() != nullptr);   // restore chain landed
}

TEST_CASE("locking aborts a live titlebar-drag move grab") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);
    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150);
    REQUIRE(a.ok());
    mapOne(server, a);
    View *v = server.viewsForTest()[0].get();
    const int x0 = v->x(), y0 = v->y();

    // Press on the titlebar (client content top-left is (161,143); the bar is
    // just above it) and drag a little: the grab is live.
    server.injectPointerMotionForTest(260, 130);
    server.injectPointerButtonForTest(BTN_LEFT, true);
    server.injectPointerMotionForTest(280, 150);
    REQUIRE(v->x() == x0 + 20);
    REQUIRE(v->y() == y0 + 20);

    test::LockTestClient lc(server.socketName());
    REQUIRE(lc.ok());
    auto pump = [&] { a.flush(); a.pump(); lc.flush(); lc.pump(); };
    lockNow(server, lc, pump);

    // Grab aborted: further motion while locked must not move the window.
    server.injectPointerMotionForTest(400, 300);
    CHECK(v->x() == x0 + 20);
    CHECK(v->y() == y0 + 20);

    lc.unlockAndDestroy();
    REQUIRE(pumpUntil(server, [&] { return !server.sessionLockForTest()->locked(); }, pump));
    // The stale release after unlock is a no-grab release: swallowed cleanly,
    // and the window still hasn't moved.
    server.injectPointerButtonForTest(BTN_LEFT, false);
    CHECK(v->x() == x0 + 20);
    CHECK(v->y() == y0 + 20);
}
