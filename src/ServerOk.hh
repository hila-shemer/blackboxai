// The ok() predicate, extracted so it's testable without standing up a real
// backend. The session is healthy only when the display and backend exist AND
// the backend actually started - a backend that's created but fails to start
// (no seat, DRM master held by another session) must report not-ok so main()
// exits loudly instead of hanging on a black screen.
#ifndef BLACKBOXAI_SERVER_OK_HH
#define BLACKBOXAI_SERVER_OK_HH

namespace bbai {

  inline bool serverStarted(bool have_display, bool have_backend, bool backend_started) {
    return have_display && have_backend && backend_started;
  }

} // namespace bbai

#endif // BLACKBOXAI_SERVER_OK_HH
