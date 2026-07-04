// The locked keys reach the behaviors through the real onKey/executeAction path
// (injectKeyForTest mirrors it). One case is enough per action - the geometry
// is already pinned by fullscreen/snap tests; this proves the wiring.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
  }
  View *mapOne(Server &s, test::TestClient &c) {
    REQUIRE(c.ok());
    auto mapped = [&] { const auto &v = s.viewsForTest();
                        return !v.empty() && v.back()->isMapped(); };
    for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); s.dispatch(); c.pump(); }
    REQUIRE(mapped());
    for (int i = 0; i < 30; ++i) { c.flush(); s.dispatch(); c.pump(); }
    return s.viewsForTest().back().get();
  }
}

TEST_CASE("Super+F toggles fullscreen on the focused view") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  server.focusViewForTest(v);

  server.injectKeyForTest(XKB_KEY_f, WLR_MODIFIER_LOGO, true);
  server.injectKeyForTest(XKB_KEY_f, WLR_MODIFIER_LOGO, false);
  for (int i = 0; i < 40; ++i) { c.flush(); server.dispatch(); c.pump(); }
  CHECK(v->isFullscreen());

  server.injectKeyForTest(XKB_KEY_f, WLR_MODIFIER_LOGO, true);
  server.injectKeyForTest(XKB_KEY_f, WLR_MODIFIER_LOGO, false);
  for (int i = 0; i < 40; ++i) { c.flush(); server.dispatch(); c.pump(); }
  CHECK_FALSE(v->isFullscreen());
}
