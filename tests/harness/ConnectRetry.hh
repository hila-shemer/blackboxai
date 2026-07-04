// Bounded-retry wl_display_connect for the harness clients. The program
// plan's watch item recorded lock_interactions failing once on a cold-cache
// parallel run at REQUIRE(lc.ok()): connect to the server's unix socket can
// transiently fail under load (listen backlog), and a one-shot connect turns
// that into a hard test failure. Retry the connect - same idiom as
// SniMockItem's D-Bus retry - and never loosen the assertions.
#ifndef BLACKBOXAI_CONNECT_RETRY_HH
#define BLACKBOXAI_CONNECT_RETRY_HH

#include <wayland-client-core.h>
#include <unistd.h>

namespace bbai::test {

  // ~500ms bound (50 x 10ms). The bound only delays the genuinely-broken
  // case; a healthy server connects on the first attempt.
  inline wl_display *connectWithRetry(const char *name) {
    for (int i = 0; i < 50; ++i) {
      if (wl_display *d = wl_display_connect(name)) return d;
      usleep(10 * 1000);
    }
    return nullptr;
  }

} // namespace bbai::test

#endif // BLACKBOXAI_CONNECT_RETRY_HH
