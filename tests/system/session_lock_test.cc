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
