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
#include "Frame.hh"

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
    // Wave-2 placement moved the map default off (160,120); restore it so this
    // wave-1 test keeps the geometry it was written against.
    server.viewsForTest().back()->setPosition(160, 120);
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

TEST_CASE("a titlebar-button press pending at lock time is dropped, not left stale") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);
    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    test::TestClient app(server.socketName(), 0xFFFF0000u, 200, 150);
    REQUIRE(app.ok());
    mapOne(server, app);
    View *v = server.viewsForTest()[0].get();

    // Press (and hold) the iconify button: pressed_button_view_ is armed, no
    // grab - the action waits for a release-inside that never arrives.
    const int bx = v->x() + frame::iconifyButton(v->contentWidth(), v->contentHeight()).x
                          + frame::kButtonWidth / 2;
    const int by = v->y() + frame::iconifyButton(v->contentWidth(), v->contentHeight()).y
                          + frame::kButtonWidth / 2;
    server.injectPointerMotionForTest(bx, by);
    REQUIRE(server.partAtForTest(bx, by) == Part::IconifyButton);
    server.injectPointerButtonForTest(BTN_LEFT, true);

    test::LockTestClient lc(server.socketName());
    REQUIRE(lc.ok());
    auto pump = [&] { app.flush(); app.pump(); lc.flush(); lc.pump(); };
    lockNow(server, lc, pump);

    // The physical release lands while locked: the gate discards it, so only
    // handleSessionLocked can have cleared the pending press.
    server.injectPointerButtonForTest(BTN_LEFT, false);

    lc.unlockAndDestroy();
    REQUIRE(pumpUntil(server, [&] { return !server.sessionLockForTest()->locked(); },
                      pump));

    SUBCASE("a bare release over the button must not fire the pre-lock action") {
        server.injectPointerMotionForTest(bx, by);
        server.injectPointerButtonForTest(BTN_LEFT, false);
        CHECK_FALSE(v->isIconified());
    }

    SUBCASE("a fresh click on the client area reaches the client whole") {
        for (int i = 0; i < 40; ++i) { pump(); server.dispatch(); }
        const int before = app.pointerButtonEvents();
        const int cx = v->x() + frame::clientX() + 50;
        const int cy = v->y() + frame::clientY() + 50;
        server.injectPointerMotionForTest(cx, cy);
        server.injectPointerButtonForTest(BTN_LEFT, true);
        server.injectPointerButtonForTest(BTN_LEFT, false);
        for (int i = 0; i < 40; ++i) { pump(); server.dispatch(); }
        CHECK(app.pointerButtonEvents() == before + 2);   // press AND release
        CHECK_FALSE(v->isIconified());
    }
}

TEST_CASE("a window mapping under the lock must not steal the locker's keyboard") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);
    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    test::LockTestClient lc(server.socketName());
    REQUIRE(lc.ok());
    auto pumpL = [&] { lc.flush(); lc.pump(); };
    REQUIRE(pumpUntil(server,
        [&] { return lc.sawLockManager() && lc.outputCount() >= 1; }, pumpL));
    lc.lock();
    REQUIRE(pumpUntil(server,
        [&] { return server.sessionLockForTest()->locked(); }, pumpL));
    lc.createLockSurface(0, 0xFF00FF00u);
    REQUIRE(pumpUntil(server,
        [&] { return server.sessionLockForTest()->focusedLockSurface() != nullptr; },
        pumpL));
    wlr_surface *locker = server.focusedKeyboardSurfaceForTest();
    REQUIRE(locker != nullptr);
    REQUIRE(locker == server.sessionLockForTest()->focusedLockSurface());

    // A client maps mid-lock. focusNewWindows (default True) must not re-point
    // the seat at it - the locked onKey branch forwards every key to the seat's
    // focused surface, i.e. the password would land in this app.
    test::TestClient app(server.socketName(), 0xFFFF0000u, 200, 150);
    REQUIRE(app.ok());
    auto pumpBoth = [&] { app.flush(); app.pump(); lc.flush(); lc.pump(); };
    mapOne(server, app);
    for (int i = 0; i < 40; ++i) { pumpBoth(); server.dispatch(); }

    CHECK(server.focusedViewForTest() == nullptr);
    CHECK(server.focusedKeyboardSurfaceForTest() == locker);

    // Unlock still lands focus on a client (the map bookkeeping survived).
    lc.unlockAndDestroy();
    REQUIRE(pumpUntil(server, [&] { return !server.sessionLockForTest()->locked(); },
                      pumpBoth));
    CHECK(server.focusedViewForTest() != nullptr);
}

TEST_CASE("a client mapping before the lock surface must not block its keyboard") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);
    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    test::LockTestClient lc(server.socketName());
    REQUIRE(lc.ok());
    auto pumpL = [&] { lc.flush(); lc.pump(); };
    REQUIRE(pumpUntil(server,
        [&] { return lc.sawLockManager() && lc.outputCount() >= 1; }, pumpL));
    lc.lock();
    REQUIRE(pumpUntil(server,
        [&] { return server.sessionLockForTest()->locked(); }, pumpL));

    // The ordering variant: the client wins the race and maps FIRST. If its
    // map takes the seat, the lock surface's null-focus guard never fires and
    // the user types their password into the void.
    test::TestClient app(server.socketName(), 0xFFFF0000u, 200, 150);
    REQUIRE(app.ok());
    mapOne(server, app);

    lc.createLockSurface(0, 0xFF00FF00u);
    auto pumpBoth = [&] { app.flush(); app.pump(); lc.flush(); lc.pump(); };
    REQUIRE(pumpUntil(server,
        [&] { return server.sessionLockForTest()->focusedLockSurface() != nullptr; },
        pumpBoth));
    CHECK(server.focusedKeyboardSurfaceForTest() ==
          server.sessionLockForTest()->focusedLockSurface());
    CHECK(server.focusedKeyboardSurfaceForTest() != nullptr);
}

TEST_CASE("destroying the focused lock surface hands the keyboard to a survivor") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);
    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);
    server.addHeadlessOutputForTest(800, 600);
    REQUIRE(pumpUntil(server, [&] { return server.outputCountForTest() == 2; },
                      [&] {}));

    test::LockTestClient lc(server.socketName());
    REQUIRE(lc.ok());
    auto pump = [&] { lc.flush(); lc.pump(); };
    REQUIRE(pumpUntil(server,
        [&] { return lc.sawLockManager() && lc.outputCount() == 2; }, pump));
    lc.lock();
    REQUIRE(pumpUntil(server,
        [&] { return server.sessionLockForTest()->locked(); }, pump));

    // Surface A first, alone, so it deterministically holds the keyboard;
    // then surface B on the second head.
    lc.createLockSurface(0, 0xFF00FF00u);
    REQUIRE(pumpUntil(server,
        [&] { return server.sessionLockForTest()->mappedLockSurfaceCountForTest() == 1; },
        pump));
    wlr_surface *first = server.focusedKeyboardSurfaceForTest();
    REQUIRE(first != nullptr);
    REQUIRE(first == server.sessionLockForTest()->focusedLockSurface());
    lc.createLockSurface(1, 0xFF0000FFu);
    REQUIRE(pumpUntil(server,
        [&] { return server.sessionLockForTest()->mappedLockSurfaceCountForTest() == 2; },
        pump));
    CHECK(server.focusedKeyboardSurfaceForTest() == first);   // B mapping steals nothing

    // The focused surface dies (multi-head locker dropping one head's surface):
    // the session stays locked and the keyboard must land on the survivor -
    // or the user cannot type their password anywhere.
    lc.destroyLockSurface(0);
    REQUIRE(pumpUntil(server,
        [&] { return server.sessionLockForTest()->mappedLockSurfaceCountForTest() == 1; },
        pump));
    CHECK(server.sessionLockForTest()->locked());
    wlr_surface *survivor = server.sessionLockForTest()->focusedLockSurface();
    REQUIRE(survivor != nullptr);
    CHECK(survivor != first);
    CHECK(server.focusedKeyboardSurfaceForTest() == survivor);

    // Clean unlock afterwards: the surface bookkeeping dropped the dead entry.
    lc.unlockAndDestroy();
    REQUIRE(pumpUntil(server, [&] { return !server.sessionLockForTest()->locked(); },
                      pump));
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
