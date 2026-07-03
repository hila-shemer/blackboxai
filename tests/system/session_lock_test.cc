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

#include <cstdlib>
#include <unistd.h>

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
