#include "Clock.hh"

#include <ctime>

namespace bt {

  int64_t SystemClock::nowMs(void) const {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
  }

  int64_t SystemClock::wallSeconds(void) const {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec);
  }

  std::string formatClock(int64_t wall_seconds, const char *fmt) {
    std::time_t t = static_cast<std::time_t>(wall_seconds);
    std::tm tmv{};
    gmtime_r(&t, &tmv);  // UTC -> TZ-independent rendering
    char buf[64];
    size_t n = std::strftime(buf, sizeof buf, fmt, &tmv);
    return std::string(buf, n);
  }

} // namespace bt
