// A4: bbai::Timer + TimerRegistry pure fireDue core (loop = nullptr).
#include <doctest/doctest.h>
#include "Timer.hh"
#include "Clock.hh"

using namespace bbai;

namespace {
  struct Counter : TimeoutHandler {
    int n = 0;
    void timeout(void) override { ++n; }
  };
}

TEST_CASE("a recurring timer fires once per due interval and re-arms") {
  bt::VirtualClock clk(0, 0);
  TimerRegistry reg(clk);
  Counter h;
  Timer t(reg, h);
  t.start(1000, /*recurring=*/true);
  CHECK(t.active());
  CHECK(t.nextFire() == 1000);

  reg.fireDue(999);          // not yet due
  CHECK(h.n == 0);
  reg.fireDue(1000);         // due -> fire, re-arm to 2000
  CHECK(h.n == 1);
  CHECK(t.nextFire() == 2000);
  reg.fireDue(1500);         // not due again
  CHECK(h.n == 1);
  reg.fireDue(2000);
  CHECK(h.n == 2);
  // A long gap fires once and skips ahead to the next future tick.
  reg.fireDue(10000);
  CHECK(h.n == 3);
  CHECK(t.nextFire() == 11000);
}

TEST_CASE("a one-shot timer fires once and does not re-arm") {
  bt::VirtualClock clk(0, 0);
  TimerRegistry reg(clk);
  Counter h;
  Timer t(reg, h);
  t.start(500, /*recurring=*/false);
  reg.fireDue(500);
  CHECK(h.n == 1);
  CHECK_FALSE(t.active());
  reg.fireDue(1000);
  CHECK(h.n == 1);
}

TEST_CASE("stop() and RAII destruction unregister the timer") {
  bt::VirtualClock clk(0, 0);
  TimerRegistry reg(clk);
  Counter h;
  {
    Timer t(reg, h);
    t.start(100, true);
    t.stop();
    reg.fireDue(100);
    CHECK(h.n == 0);          // stopped before firing
  }
  Counter h2;
  {
    Timer t(reg, h2);
    t.start(100, true);
  }                            // dtor -> remove
  reg.fireDue(100000);
  CHECK(h2.n == 0);            // a destroyed timer never fires
}

TEST_CASE("multiple due timers all fire on one fireDue") {
  bt::VirtualClock clk(0, 0);
  TimerRegistry reg(clk);
  Counter a, b;
  Timer ta(reg, a), tb(reg, b);
  ta.start(1000, true);
  tb.start(1000, true);
  reg.fireDue(1000);
  CHECK(a.n == 1);
  CHECK(b.n == 1);
}
