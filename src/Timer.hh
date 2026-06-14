// A recurring/one-shot timer abstraction whose firing logic is pure and
// L0-unit-testable: TimerRegistry::fireDue(now_ms) drives everything, so tests
// (loop == nullptr) advance the injected bt::Clock and call fireDue by hand. The
// production wl_event_loop wiring (a single timer source re-armed to the earliest
// due time) is added when the Server owns the registry (M4-A6). Modeled on
// blackboxwm's Timer/TimerQueue but clock-injected and scene-free.
#ifndef BLACKBOXAI_TIMER_HH
#define BLACKBOXAI_TIMER_HH

#include "Clock.hh"

#include <cstdint>
#include <vector>

struct wl_event_loop;
struct wl_event_source;

namespace bbai {

  class TimeoutHandler {
  public:
    virtual ~TimeoutHandler() = default;
    virtual void timeout(void) = 0;
  };

  class TimerRegistry;

  class Timer {
  public:
    Timer(TimerRegistry &registry, TimeoutHandler &handler);
    ~Timer();
    Timer(const Timer &) = delete;
    Timer &operator=(const Timer &) = delete;

    void start(int64_t interval_ms, bool recurring);  // arm: fire at now + interval
    void stop(void);
    bool active(void) const { return active_; }
    int64_t nextFire(void) const { return next_fire_ms_; }

  private:
    friend class TimerRegistry;
    TimerRegistry &registry_;
    TimeoutHandler &handler_;
    int64_t interval_ms_ = 0;
    int64_t next_fire_ms_ = 0;
    bool recurring_ = false;
    bool active_ = false;
  };

  class TimerRegistry {
  public:
    explicit TimerRegistry(bt::Clock &clock, wl_event_loop *loop = nullptr);
    ~TimerRegistry();
    TimerRegistry(const TimerRegistry &) = delete;
    TimerRegistry &operator=(const TimerRegistry &) = delete;

    bt::Clock &clock(void) { return clock_; }

    void add(Timer *t);     // called by Timer::start
    void remove(Timer *t);  // called by Timer::stop / ~Timer

    // Fire every active timer whose next_fire_ms <= now_ms (re-arming recurring
    // ones), then reschedule the production wake-up. Safe if a handler starts/
    // stops timers (operates on a snapshot of the due set).
    void fireDue(int64_t now_ms);

  private:
    void reschedule(void);  // production: arm the wl_event_loop source (no-op if loop==null)

    bt::Clock &clock_;
    wl_event_loop *loop_;
    wl_event_source *source_ = nullptr;
    std::vector<Timer *> timers_;
  };

} // namespace bbai

#endif // BLACKBOXAI_TIMER_HH
