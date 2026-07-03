// ext-idle-notify-v1: the compositor's whole job is notify_activity on every
// input event (wlroots owns the timers) + set_inhibited. timeout=0 makes it
// deterministic: idled fires immediately, activity sends resumed+idled - no
// real-time waits anywhere.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "LockTestClient.hh"
#include "Server.hh"
#include "SessionLock.hh"

#include <cstdlib>

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

} // namespace

TEST_CASE("timeout=0 notification: idled at once, resumed+idled on activity") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    test::IdleTestClient ic(server.socketName());
    REQUIRE(ic.ok());
    ic.createNotification(0);
    REQUIRE(pumpUntil(server, [&] { return ic.idledCount() == 1; },
                      [&] { ic.flush(); ic.pump(); }));
    CHECK(ic.resumedCount() == 0);

    // Pointer activity through the real funnel.
    server.injectPointerMotionForTest(100, 100);
    REQUIRE(pumpUntil(server,
        [&] { return ic.resumedCount() == 1 && ic.idledCount() == 2; },
        [&] { ic.flush(); ic.pump(); }));

    // Key activity through the deviceless mirror.
    server.injectKeyForTest(XKB_KEY_a, 0, true);
    REQUIRE(pumpUntil(server,
        [&] { return ic.resumedCount() == 2 && ic.idledCount() == 3; },
        [&] { ic.flush(); ic.pump(); }));
}

TEST_CASE("set_inhibited suppresses idled until released") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    // No idle-inhibit protocol this slice: drive the knob directly, prove the
    // passthrough works end-to-end through a real notification.
    wlr_idle_notifier_v1_set_inhibited(server.idleNotifierForTest(), true);

    test::IdleTestClient ic(server.socketName());
    REQUIRE(ic.ok());
    ic.createNotification(0);
    for (int i = 0; i < 50; ++i) { ic.flush(); server.dispatch(); ic.pump(); }
    CHECK(ic.idledCount() == 0);   // inhibited: never goes idle

    wlr_idle_notifier_v1_set_inhibited(server.idleNotifierForTest(), false);
    REQUIRE(pumpUntil(server, [&] { return ic.idledCount() == 1; },
                      [&] { ic.flush(); ic.pump(); }));
}

TEST_CASE("activity while LOCKED still resets idle - notify sits above the gate") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    bootOutput(server);

    test::LockTestClient lc(server.socketName());
    test::IdleTestClient ic(server.socketName());
    REQUIRE(lc.ok());
    REQUIRE(ic.ok());
    auto pump = [&] { lc.flush(); lc.pump(); ic.flush(); ic.pump(); };
    REQUIRE(pumpUntil(server, [&] { return lc.sawLockManager(); }, pump));
    lc.lock();
    REQUIRE(pumpUntil(server, [&] { return server.sessionLockForTest()->locked(); },
                      pump));

    ic.createNotification(0);
    REQUIRE(pumpUntil(server, [&] { return ic.idledCount() == 1; }, pump));

    // Typing at the locker (deviceless mirror) is activity, gate or no gate.
    server.injectKeyForTest(XKB_KEY_a, 0, true);
    REQUIRE(pumpUntil(server,
        [&] { return ic.resumedCount() == 1 && ic.idledCount() == 2; }, pump));
}
