#include "Timer.hh"
#include "wlr.hpp"   // wl_event_loop / wl_event_source (production timer source)

#include <algorithm>
#include <cstdint>

namespace bbai {

  // wl_event_loop timer callback: fire due timers, then reschedule re-arms us.
  static int timerDispatch(void *data) {
    auto *self = static_cast<TimerRegistry *>(data);
    self->fireDue(self->clock().nowMs());
    return 0;
  }

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
    : clock_(clock), loop_(loop) {
    if (loop_) source_ = wl_event_loop_add_timer(loop_, &timerDispatch, this);
  }

  TimerRegistry::~TimerRegistry() {
    if (source_) wl_event_source_remove(source_);
  }

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
    // With loop_ == nullptr (the pure/test path) there is nothing to arm; tests
    // drive fireDue() directly. In production, arm the single source to the
    // earliest due time.
    if (!loop_ || !source_) return;
    int64_t earliest = INT64_MAX;
    for (Timer *t : timers_)
      if (t->active_) earliest = std::min(earliest, t->next_fire_ms_);
    if (earliest == INT64_MAX) {
      wl_event_source_timer_update(source_, 0);  // disarm: no active timers
      return;
    }
    int64_t delay = earliest - clock_.nowMs();
    if (delay < 1) delay = 1;
    wl_event_source_timer_update(source_, static_cast<int>(delay));
  }

} // namespace bbai
