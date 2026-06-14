// A6: the Server owns a deterministic clock + timer registry. Headless uses a
// VirtualClock fixed at 14:05:00 UTC; advanceClockForTest advances it and fires
// due timers — no wall clock, no sleep.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "Server.hh"
#include "Timer.hh"
#include "Clock.hh"

#include <cstdlib>

using namespace bbai;

namespace {
  struct Counter : TimeoutHandler {
    int n = 0;
    void timeout() override { ++n; }
  };
}

TEST_CASE("Server clock is a fixed virtual epoch; advanceClockForTest fires timers") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());

  // Fixed epoch 14:05:00 UTC -> "02:05 PM".
  CHECK(server.wallSecondsForTest() == 14 * 3600 + 5 * 60);
  CHECK(bt::formatClock(server.wallSecondsForTest()) == "02:05 PM");

  // A 60s recurring timer fires once per advanced minute.
  Counter h;
  Timer t(server.timerRegistry(), h);
  t.start(60000, /*recurring=*/true);

  server.advanceClockForTest(60);
  CHECK(h.n == 1);
  CHECK(server.wallSecondsForTest() == 14 * 3600 + 6 * 60);
  CHECK(bt::formatClock(server.wallSecondsForTest()) == "02:06 PM");

  server.advanceClockForTest(60);
  CHECK(h.n == 2);
}
