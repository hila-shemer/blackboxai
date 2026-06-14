// A3: bt::Clock / VirtualClock / formatClock — deterministic, TZ-independent.
#include <doctest/doctest.h>
#include "Clock.hh"

using namespace bt;

TEST_CASE("formatClock renders UTC, TZ-independent, blackbox default %I:%M %p") {
  CHECK(formatClock(0) == "12:00 AM");                       // 1970-01-01 00:00 UTC
  CHECK(formatClock(14 * 3600 + 5 * 60) == "02:05 PM");      // 14:05 UTC
  CHECK(formatClock(14 * 3600 + 6 * 60) == "02:06 PM");      // the tick target
  CHECK(formatClock(9 * 3600 + 30 * 60, "%H:%M") == "09:30");
  CHECK(formatClock(0, "") == "");                            // strftime-empty fallback
}

TEST_CASE("VirtualClock exposes injected time and advances both clocks") {
  VirtualClock vc(/*wall=*/100, /*now_ms=*/5000);
  CHECK(vc.wallSeconds() == 100);
  CHECK(vc.nowMs() == 5000);
  vc.advance(60);
  CHECK(vc.wallSeconds() == 160);
  CHECK(vc.nowMs() == 65000);     // 60s == 60000ms
  vc.setWall(14 * 3600 + 5 * 60);
  CHECK(formatClock(vc.wallSeconds()) == "02:05 PM");
}

TEST_CASE("SystemClock is monotonic-ish and returns a plausible wall time") {
  SystemClock c;
  const int64_t a = c.nowMs();
  const int64_t b = c.nowMs();
  CHECK(b >= a);                  // monotonic, never goes backwards
  CHECK(c.wallSeconds() > 1700000000);  // after 2023; sanity, not exact
}
