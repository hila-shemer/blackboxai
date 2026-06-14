// An injectable wall-clock + monotonic-time seam for the toolbar clock. Pure
// (no wlroots): production uses SystemClock; tests use VirtualClock and advance
// it by hand so the ticking clock is deterministic in goldens (never wall time).
// formatClock() uses gmtime_r so the rendered string does not depend on the
// host's $TZ — the test picks a UTC epoch whose rendering is the asserted string.
#ifndef BLACKBOXAI_CLOCK_HH
#define BLACKBOXAI_CLOCK_HH

#include <cstdint>
#include <string>

namespace bt {

  class Clock {
  public:
    virtual ~Clock() = default;
    virtual int64_t nowMs(void) const = 0;        // monotonic ms (timer scheduling)
    virtual int64_t wallSeconds(void) const = 0;  // wall-clock seconds since the epoch
  };

  class SystemClock : public Clock {
  public:
    int64_t nowMs(void) const override;           // CLOCK_MONOTONIC
    int64_t wallSeconds(void) const override;      // CLOCK_REALTIME
  };

  class VirtualClock : public Clock {
  public:
    explicit VirtualClock(int64_t wall_seconds = 0, int64_t now_ms = 0)
      : wall_seconds_(wall_seconds), now_ms_(now_ms) {}
    int64_t nowMs(void) const override { return now_ms_; }
    int64_t wallSeconds(void) const override { return wall_seconds_; }
    void advance(int64_t seconds) { wall_seconds_ += seconds; now_ms_ += seconds * 1000; }
    void setWall(int64_t seconds) { wall_seconds_ = seconds; }
  private:
    int64_t wall_seconds_, now_ms_;
  };

  // Render `wall_seconds` (UTC, via gmtime_r) with a strftime format. Default is
  // blackbox's "%I:%M %p" (e.g. "02:05 PM"). Returns "" if strftime produces
  // nothing (e.g. an empty format).
  std::string formatClock(int64_t wall_seconds, const char *fmt = "%I:%M %p");

} // namespace bt

#endif // BLACKBOXAI_CLOCK_HH
