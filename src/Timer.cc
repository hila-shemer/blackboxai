#include "Timer.hh"

#include <algorithm>

namespace bbai {

  Timer::Timer(TimerRegistry &registry, TimeoutHandler &handler)
    : registry_(registry), handler_(handler) {}

  Timer::~Timer() { stop(); }

  void Timer::start(int64_t interval_ms, bool recurring) {
    interval_ms_ = interval_ms;
    recurring_ = recurring;
    next_fire_ms_ = registry_.clock().nowMs() + interval_ms;
    if (!active_) {
      active_ = true;
      registry_.add(this);
    }
  }

  void Timer::stop(void) {
    if (active_) {
      active_ = false;
      registry_.remove(this);
    }
  }

  TimerRegistry::TimerRegistry(bt::Clock &clock, wl_event_loop *loop)
    : clock_(clock), loop_(loop) {}

  TimerRegistry::~TimerRegistry() = default;  // source_ wired/destroyed in M4-A6

  void TimerRegistry::add(Timer *t) {
    if (std::find(timers_.begin(), timers_.end(), t) == timers_.end())
      timers_.push_back(t);
    reschedule();
  }

  void TimerRegistry::remove(Timer *t) {
    timers_.erase(std::remove(timers_.begin(), timers_.end(), t), timers_.end());
    reschedule();
  }

  void TimerRegistry::fireDue(int64_t now_ms) {
    // Snapshot the due set: a handler may start/stop timers while firing.
    std::vector<Timer *> due;
    for (Timer *t : timers_)
      if (t->active_ && t->next_fire_ms_ <= now_ms) due.push_back(t);

    for (Timer *t : due) {
      if (!t->active_) continue;  // a prior handler stopped it
      if (t->recurring_) {
        t->next_fire_ms_ += t->interval_ms_;
        if (t->next_fire_ms_ <= now_ms)        // long gap: skip to the next future tick
          t->next_fire_ms_ = now_ms + t->interval_ms_;
      } else {
        t->active_ = false;
      }
      t->handler_.timeout();
    }
    reschedule();
  }

  void TimerRegistry::reschedule(void) {
    // Production wl_event_loop wiring is added in M4-A6; with loop_ == nullptr
    // (the pure/test path) there is nothing to arm.
  }

} // namespace bbai
