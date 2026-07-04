// ext-session-lock-v1: lifecycle, blanking, input isolation, unlock restore.
// The lock client is a real wayland-client speaking the real protocol; frames
// are asserted with full-pixel scans (stronger than goldens for solid colors,
// zero BLESS churn) and pre/post-unlock frame equality.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "LockTestClient.hh"
#include "Server.hh"
#include "SessionLock.hh"
#include "View.hh"

#include <cstdlib>
#include <memory>
#include <unistd.h>
#include <linux/input-event-codes.h>

using namespace bbai;

namespace {

  void bootOutput(Server &server) {
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
      server.dispatch();
    REQUIRE(server.activeSceneOutputForTest() != nullptr);
  }

  // Bounded event-wait. sleep_us > 0 only where the condition depends on the
  // headless backend's real-time frame timer (the send_locked commit path);
  // everything else runs it with sleep_us = 0.
  template <typename Cond, typename Pump>
  bool pumpUntil(Server &server, Cond cond, Pump pump, int iters = 1000,
                 int sleep_us = 0) {
    for (int i = 0; i < iters && !cond(); ++i) {
      pump();
      server.dispatch();
      pump();
      if (sleep_us > 0) usleep(sleep_us);
    }
    return cond();
  }

} // namespace

TEST_CASE("lock manager, idle notifier and wl_output globals are advertised") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    test::LockTestClient lc(server.socketName());
    REQUIRE(lc.ok());
    bool bound = pumpUntil(server,
        [&] { return lc.sawLockManager() && lc.sawIdleNotifier() && lc.outputCount() >= 1; },
        [&] { lc.flush(); lc.pump(); });
    CHECK(bound);
    CHECK(lc.outputCount() == 1);
    CHECK(server.sessionLockForTest() != nullptr);
    CHECK_FALSE(server.sessionLockForTest()->locked());
}

TEST_CASE("lock blanks every head and sends locked after the post-blank commit") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    test::LockTestClient lc(server.socketName());
    REQUIRE(lc.ok());
    REQUIRE(pumpUntil(server, [&] { return lc.sawLockManager(); },
                      [&] { lc.flush(); lc.pump(); }));

    lc.lock();
    // The ONE bounded real-time wait in this plan: the post-blank commit rides
    // the headless backend's ~16ms frame timer, which only real time advances.
    bool locked = pumpUntil(server, [&] { return lc.lockedReceived(); },
                            [&] { lc.flush(); lc.pump(); },
                            /*iters=*/1000, /*sleep_us=*/2000);
    CHECK(locked);
    CHECK(server.sessionLockForTest()->locked());
    CHECK(server.sessionLockForTest()->lockedSentForTest());
    CHECK(server.sessionLockForTest()->blankRectCountForTest() == 1);
    CHECK_FALSE(lc.finishedReceived());

    // The blank obligation: with no lock surface, every pixel is opaque black.
    test::Frame f = test::captureFrame(server);
    REQUIRE(f.w == 1280u);
    REQUIRE(f.h == 720u);
    size_t non_black = 0;
    for (uint32_t p : f.pixels)
        if ((p & 0x00FFFFFFu) != 0u) ++non_black;
    CHECK(non_black == 0u);
}

TEST_CASE("fallback timer sends locked without waiting on frame timing") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    test::LockTestClient lc(server.socketName());
    REQUIRE(lc.ok());
    REQUIRE(pumpUntil(server, [&] { return lc.sawLockManager(); },
                      [&] { lc.flush(); lc.pump(); }));

    lc.lock();
    // A few zero-sleep pumps deliver the lock request; the ~16ms frame timer
    // cannot have fired inside them, so locked can only come from the fallback.
    pumpUntil(server, [&] { return server.sessionLockForTest()->locked(); },
              [&] { lc.flush(); lc.pump(); });
    REQUIRE(server.sessionLockForTest()->locked());
    server.advanceClockForTest(2);   // > kLockedFallbackMs; fires the one-shot
    bool locked = pumpUntil(server, [&] { return lc.lockedReceived(); },
                            [&] { lc.flush(); lc.pump(); });
    CHECK(locked);
    CHECK(server.sessionLockForTest()->lockedSentForTest());
}

TEST_CASE("a second lock while one is active is denied with finished") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    test::LockTestClient first(server.socketName());
    test::LockTestClient second(server.socketName());
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    auto pumpBoth = [&] { first.flush(); first.pump(); second.flush(); second.pump(); };
    REQUIRE(pumpUntil(server,
        [&] { return first.sawLockManager() && second.sawLockManager(); }, pumpBoth));

    first.lock();
    REQUIRE(pumpUntil(server, [&] { return server.sessionLockForTest()->locked(); },
                      pumpBoth));

    second.lock();
    bool denied = pumpUntil(server, [&] { return second.finishedReceived(); }, pumpBoth);
    CHECK(denied);
    CHECK_FALSE(second.lockedReceived());
    CHECK(server.sessionLockForTest()->locked());   // first still holds it
}

TEST_CASE("a lock surface is configured to output size, rendered, and keyboard-focused") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    test::LockTestClient lc(server.socketName());
    REQUIRE(lc.ok());
    REQUIRE(pumpUntil(server,
        [&] { return lc.sawLockManager() && lc.outputCount() >= 1; },
        [&] { lc.flush(); lc.pump(); }));

    lc.lock();
    REQUIRE(pumpUntil(server, [&] { return server.sessionLockForTest()->locked(); },
                      [&] { lc.flush(); lc.pump(); }));

    lc.createLockSurface(0, 0xFF00FF00u);   // opaque green
    bool configured = pumpUntil(server,
        [&] { return lc.configuredWidth(0) == 1280 && lc.configuredHeight(0) == 720; },
        [&] { lc.flush(); lc.pump(); });
    REQUIRE(configured);

    // Buffer attached + committed -> mapped -> keyboard focus + full green frame.
    bool mapped = pumpUntil(server,
        [&] { return server.sessionLockForTest()->focusedLockSurface() != nullptr; },
        [&] { lc.flush(); lc.pump(); });
    REQUIRE(mapped);
    CHECK(server.focusedKeyboardSurfaceForTest() ==
          server.sessionLockForTest()->focusedLockSurface());

    test::Frame f = test::captureFrame(server);
    REQUIRE(f.w == 1280u);
    REQUIRE(f.h == 720u);
    size_t non_green = 0;
    for (uint32_t p : f.pixels)
        if ((p & 0x00FFFFFFu) != 0x0000FF00u) ++non_green;
    // Every pixel is the locker's green: desktop, toolbar, everything hidden.
    CHECK(non_green == 0u);
}

TEST_CASE("locked session: clients get no input, bindings are dead, quit key suppressed") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    // A normal client window, mapped and focused before the lock.
    test::TestClient app(server.socketName(), 0xFFFF0000u, 200, 150);
    REQUIRE(app.ok());
    auto appMapped = [&] {
        const auto &v = server.viewsForTest();
        return !v.empty() && v[0]->isMapped();
    };
    REQUIRE(pumpUntil(server, appMapped, [&] { app.flush(); app.pump(); }));
    server.viewsForTest()[0]->setPosition(160, 120);  // wave-2 placement restore
    for (int i = 0; i < 40; ++i) { app.flush(); server.dispatch(); app.pump(); }
    server.injectPointerMotionForTest(260, 130);
    server.injectPointerButtonForTest(BTN_LEFT, true);
    server.injectPointerButtonForTest(BTN_LEFT, false);
    REQUIRE(server.focusedViewForTest() == server.viewsForTest()[0].get());
    // Drain: deliver the pre-lock button events so the counter baseline is set.
    for (int i = 0; i < 40; ++i) { app.flush(); server.dispatch(); app.pump(); }
    const int buttons_before = app.pointerButtonEvents();

    test::LockTestClient lc(server.socketName());
    REQUIRE(lc.ok());
    auto pumpBoth = [&] { app.flush(); app.pump(); lc.flush(); lc.pump(); };
    REQUIRE(pumpUntil(server, [&] { return lc.sawLockManager(); }, pumpBoth));
    lc.lock();
    REQUIRE(pumpUntil(server, [&] { return server.sessionLockForTest()->locked(); },
                      pumpBoth));
    CHECK(server.focusedViewForTest() == nullptr);   // focus parked on lock

    // Pointer: press+release over where the app sits - the client must see nothing.
    server.injectPointerMotionForTest(260, 130);
    server.injectPointerButtonForTest(BTN_LEFT, true);
    server.injectPointerButtonForTest(BTN_LEFT, false);
    for (int i = 0; i < 40; ++i) { pumpBoth(); server.dispatch(); }
    CHECK(app.pointerButtonEvents() == buttons_before);
    CHECK(server.focusedPointerSurfaceForTest() == nullptr);

    // Bindings: workspace switch dead while locked.
    const unsigned ws_before = server.currentWorkspaceForTest();
    server.injectKeyForTest(XKB_KEY_Right, WLR_MODIFIER_LOGO, true);
    server.injectKeyForTest(XKB_KEY_Right, WLR_MODIFIER_LOGO, false);
    CHECK(server.currentWorkspaceForTest() == ws_before);

    // THE policy test: Ctrl+Alt+Backspace (Action::Quit) is suppressed. No
    // binding fired since the lock, so last_action_ is still the ctor default
    // (dispatch() keeps working after wl_display_terminate, so it can't be
    // the detector here - the action introspection is).
    server.injectKeyForTest(XKB_KEY_BackSpace,
                            WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, true);
    server.injectKeyForTest(XKB_KEY_BackSpace,
                            WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, false);
    CHECK(server.lastActionForTest() == Action::None);
    CHECK(server.sessionLockForTest()->locked());

    // Menu binding dead too: Super+Space must not open the root menu.
    server.injectKeyForTest(XKB_KEY_space, WLR_MODIFIER_LOGO, true);
    CHECK_FALSE(server.menuOpenForTest());
}

TEST_CASE("unlock restores the desktop pixel-for-pixel, focus and bindings included") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    test::TestClient app(server.socketName(), 0xFFFF0000u, 200, 150);
    REQUIRE(app.ok());
    auto appMapped = [&] {
        const auto &v = server.viewsForTest();
        return !v.empty() && v[0]->isMapped();
    };
    REQUIRE(pumpUntil(server, appMapped, [&] { app.flush(); app.pump(); }));
    server.viewsForTest()[0]->setPosition(160, 120);  // wave-2 placement restore
    for (int i = 0; i < 40; ++i) { app.flush(); server.dispatch(); app.pump(); }
    server.injectPointerMotionForTest(260, 130);
    server.injectPointerButtonForTest(BTN_LEFT, true);
    server.injectPointerButtonForTest(BTN_LEFT, false);
    View *focused_before = server.focusedViewForTest();
    REQUIRE(focused_before != nullptr);
    for (int i = 0; i < 40; ++i) { app.flush(); server.dispatch(); app.pump(); }

    test::Frame before = test::captureFrame(server);

    test::LockTestClient lc(server.socketName());
    REQUIRE(lc.ok());
    auto pumpBoth = [&] { app.flush(); app.pump(); lc.flush(); lc.pump(); };
    REQUIRE(pumpUntil(server, [&] { return lc.sawLockManager(); }, pumpBoth));
    lc.lock();
    REQUIRE(pumpUntil(server, [&] { return server.sessionLockForTest()->locked(); },
                      pumpBoth));
    server.advanceClockForTest(2);   // fallback path; no real-time wait needed here
    REQUIRE(pumpUntil(server, [&] { return lc.lockedReceived(); }, pumpBoth));

    lc.unlockAndDestroy();
    bool unlocked = pumpUntil(server,
        [&] { return !server.sessionLockForTest()->locked(); }, pumpBoth);
    REQUIRE(unlocked);
    CHECK(server.sessionLockForTest()->blankRectCountForTest() == 0);
    CHECK_FALSE(server.sessionLockForTest()->hasActiveLockForTest());

    // Focus is back on the pre-lock window.
    CHECK(server.focusedViewForTest() == focused_before);

    // The desktop came back exactly - captured BEFORE the binding checks below
    // perturb anything. advanceClockForTest(2) stayed inside the same clock
    // minute (14:05:00 epoch), so the toolbar clock text is stable.
    test::Frame after = test::captureFrame(server);
    REQUIRE(after.w == before.w);
    REQUIRE(after.h == before.h);
    CHECK(after.pixels == before.pixels);

    // And bindings are live again.
    const unsigned ws_before = server.currentWorkspaceForTest();
    server.injectKeyForTest(XKB_KEY_Right, WLR_MODIFIER_LOGO, true);
    CHECK(server.currentWorkspaceForTest() == (ws_before + 1) % 4);
}

TEST_CASE("a crashed locker leaves the session locked; a new locker takes over") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    auto first = std::make_unique<test::LockTestClient>(server.socketName());
    REQUIRE(first->ok());
    REQUIRE(pumpUntil(server, [&] { return first->sawLockManager(); },
                      [&] { first->flush(); first->pump(); }));
    first->lock();
    REQUIRE(pumpUntil(server, [&] { return server.sessionLockForTest()->locked(); },
                      [&] { first->flush(); first->pump(); }));
    server.advanceClockForTest(2);
    REQUIRE(pumpUntil(server, [&] { return first->lockedReceived(); },
                      [&] { first->flush(); first->pump(); }));

    // Crash the locker: disconnect without unlock (the dtor sends no destroy
    // for a locked lock on purpose - INVALID_DESTROY would be a protocol error).
    first.reset();
    bool abandoned = pumpUntil(server,
        [&] { return !server.sessionLockForTest()->hasActiveLockForTest(); },
        [&] {});
    REQUIRE(abandoned);

    // Still locked: state, pixels, and input gate all hold.
    CHECK(server.sessionLockForTest()->locked());
    CHECK(server.sessionLockForTest()->blankRectCountForTest() == 1);
    test::Frame f = test::captureFrame(server);
    size_t non_black = 0;
    for (uint32_t p : f.pixels)
        if ((p & 0x00FFFFFFu) != 0u) ++non_black;
    CHECK(non_black == 0u);
    const unsigned ws_before = server.currentWorkspaceForTest();
    server.injectKeyForTest(XKB_KEY_Right, WLR_MODIFIER_LOGO, true);
    CHECK(server.currentWorkspaceForTest() == ws_before);

    // Recovery: a fresh locker locks again. Every head already committed its
    // post-blank frame, so locked arrives with zero-sleep pumping - the
    // takeover short-circuit, asserted deterministically.
    test::LockTestClient second(server.socketName());
    REQUIRE(second.ok());
    REQUIRE(pumpUntil(server, [&] { return second.sawLockManager(); },
                      [&] { second.flush(); second.pump(); }));
    second.lock();
    bool relocked = pumpUntil(server, [&] { return second.lockedReceived(); },
                              [&] { second.flush(); second.pump(); });
    CHECK(relocked);
    CHECK_FALSE(second.finishedReceived());
    CHECK(server.sessionLockForTest()->blankRectCountForTest() == 1);  // no double-blank

    // And the takeover unlocks cleanly.
    second.unlockAndDestroy();
    REQUIRE(pumpUntil(server, [&] { return !server.sessionLockForTest()->locked(); },
                      [&] { second.flush(); second.pump(); }));
}

TEST_CASE("head destroyed mid-lock-wait: dropped from the wait, locked still sent") {
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
    REQUIRE(pumpUntil(server, [&] { return lc.sawLockManager(); },
                      [&] { lc.flush(); lc.pump(); }));
    lc.lock();
    // Zero-sleep pumps: the lock request lands and both heads get blanked, but
    // the ~16ms frame timer can't have fired inside them - we are mid-wait
    // (same reasoning as the fallback-timer case above).
    REQUIRE(pumpUntil(server, [&] {
        return server.sessionLockForTest()->locked()
            && server.sessionLockForTest()->blankRectCountForTest() == 2;
    }, [&] { lc.flush(); lc.pump(); }));
    REQUIRE_FALSE(server.sessionLockForTest()->lockedSentForTest());

    // Hot-unplug the second head mid-wait: its blank dies, its entry leaves
    // the wait set, and the recount is what lets `locked` go out at all -
    // head 1 never commits a post-blank frame.
    server.destroyOutputForTest(1);
    CHECK(server.sessionLockForTest()->blankRectCountForTest() == 1);

    bool got_locked = pumpUntil(server, [&] { return lc.lockedReceived(); },
                                [&] { lc.flush(); lc.pump(); },
                                /*iters=*/3000, /*sleep_us=*/1000);
    CHECK(got_locked);
    CHECK(server.sessionLockForTest()->lockedSentForTest());

    lc.unlockAndDestroy();
    REQUIRE(pumpUntil(server, [&] { return !server.sessionLockForTest()->locked(); },
                      [&] { lc.flush(); lc.pump(); }));
}

TEST_CASE("takeover keeps the pre-lock focus for the eventual unlock") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    // A mapped and focused before the lock.
    test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150);
    REQUIRE(a.ok());
    auto aMapped = [&] {
        const auto &v = server.viewsForTest();
        return !v.empty() && v[0]->isMapped();
    };
    REQUIRE(pumpUntil(server, aMapped, [&] { a.flush(); a.pump(); }));
    server.viewsForTest()[0]->setPosition(160, 120);  // wave-2 placement restore
    for (int i = 0; i < 40; ++i) { a.flush(); server.dispatch(); a.pump(); }
    server.injectPointerMotionForTest(260, 130);
    server.injectPointerButtonForTest(BTN_LEFT, true);
    server.injectPointerButtonForTest(BTN_LEFT, false);
    View *va = server.viewsForTest()[0].get();
    REQUIRE(server.focusedViewForTest() == va);

    auto first = std::make_unique<test::LockTestClient>(server.socketName());
    REQUIRE(first->ok());
    REQUIRE(pumpUntil(server, [&] { return first->sawLockManager(); },
                      [&] { first->flush(); first->pump(); }));
    first->lock();
    REQUIRE(pumpUntil(server, [&] { return server.sessionLockForTest()->locked(); },
                      [&] { first->flush(); first->pump(); }));
    server.advanceClockForTest(2);
    REQUIRE(pumpUntil(server, [&] { return first->lockedReceived(); },
                      [&] { first->flush(); first->pump(); }));

    // B maps mid-lock: top of the stacking layer, but focus stays parked - so
    // topmost and pre-lock-focused now genuinely diverge.
    test::TestClient b(server.socketName(), 0xFF00FF00u, 200, 150);
    REQUIRE(b.ok());
    auto pumpAB = [&] { a.flush(); a.pump(); b.flush(); b.pump();
                        first->flush(); first->pump(); };
    REQUIRE(pumpUntil(server,
        [&] { return server.viewsForTest().size() == 2
                  && server.viewsForTest()[1]->isMapped(); }, pumpAB));
    // Wave-2 placement moved B off (160,120); restore the overlap so the leftover
    // cursor at (260,130) sits over a TITLEBAR (not B's client) - otherwise the
    // post-unlock onPointerMotion sloppy-refocuses B instead of restoring va.
    for (auto &up : server.viewsForTest()) up->setPosition(160, 120);
    REQUIRE(server.focusedViewForTest() == nullptr);

    // Locker crash, then a takeover locker.
    first.reset();
    REQUIRE(pumpUntil(server,
        [&] { return !server.sessionLockForTest()->hasActiveLockForTest(); },
        [&] {}));
    test::LockTestClient second(server.socketName());
    REQUIRE(second.ok());
    REQUIRE(pumpUntil(server, [&] { return second.sawLockManager(); },
                      [&] { second.flush(); second.pump(); }));
    second.lock();
    REQUIRE(pumpUntil(server, [&] { return second.lockedReceived(); },
                      [&] { second.flush(); second.pump(); }));

    // Unlock must restore the window the user had focused when the session
    // locked - not the topmost fallback (B).
    second.unlockAndDestroy();
    auto pumpAll = [&] { a.flush(); a.pump(); b.flush(); b.pump();
                         second.flush(); second.pump(); };
    REQUIRE(pumpUntil(server, [&] { return !server.sessionLockForTest()->locked(); },
                      pumpAll));
    CHECK(server.focusedViewForTest() == va);
}

TEST_CASE("a head appearing mid-lock is blanked and its lock surface configured") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    test::LockTestClient lc(server.socketName());
    REQUIRE(lc.ok());
    REQUIRE(pumpUntil(server, [&] { return lc.sawLockManager(); },
                      [&] { lc.flush(); lc.pump(); }));
    lc.lock();
    REQUIRE(pumpUntil(server, [&] { return server.sessionLockForTest()->locked(); },
                      [&] { lc.flush(); lc.pump(); }));
    server.advanceClockForTest(2);
    REQUIRE(pumpUntil(server, [&] { return lc.lockedReceived(); },
                      [&] { lc.flush(); lc.pump(); }));
    REQUIRE(server.sessionLockForTest()->blankRectCountForTest() == 1);

    // A second monitor lights up under the lock.
    server.addHeadlessOutputForTest(800, 600);
    REQUIRE(pumpUntil(server, [&] { return server.outputCountForTest() == 2; },
                      [&] { lc.flush(); lc.pump(); }));
    CHECK(server.sessionLockForTest()->blankRectCountForTest() == 2);

    // The client sees the new wl_output and covers it; the compositor
    // configures the late lock surface to the new head's size.
    REQUIRE(pumpUntil(server, [&] { return lc.outputCount() == 2; },
                      [&] { lc.flush(); lc.pump(); }));
    lc.createLockSurface(1, 0xFF0000FFu);
    // This test creates only ONE lock surface, so it is index 0 in the
    // client's list even though it targets output 1.
    bool configured = pumpUntil(server,
        [&] { return lc.configuredWidth(0) == 800 && lc.configuredHeight(0) == 600; },
        [&] { lc.flush(); lc.pump(); });
    CHECK(configured);
}
