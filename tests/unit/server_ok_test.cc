// M7: ok() must mean the backend actually started, not merely that the objects
// exist - a created-but-unstarted backend (no DRM master / seat held elsewhere)
// otherwise drops you into wl_display_run staring at black. The predicate is
// extracted so it's testable without faking a backend.
#include <doctest/doctest.h>
#include "ServerOk.hh"
using namespace bbai;

TEST_CASE("ok() requires the backend to have started") {
  CHECK(serverStarted(true, true, true));
  CHECK_FALSE(serverStarted(true, true, false));   // created but start failed
  CHECK_FALSE(serverStarted(true, false, true));
  CHECK_FALSE(serverStarted(false, true, true));
}
