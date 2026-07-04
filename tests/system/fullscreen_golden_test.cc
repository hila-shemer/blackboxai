// The ONE thing only a frame can prove: a focused fullscreen view sits ABOVE
// the toolbar (layer_fullscreen is between layer_top and layer_overlay), so no
// bar pixels show. Also the classic demote rule: an UNFOCUSED fullscreen view
// drops back to layer_window, so an alt-tab preview of a normal window is
// visible over it. Runs under text_env: the reference frame would otherwise
// contain the toolbar's clock/label text (gotcha #20).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"
#include "Output.hh"

#include <cstdlib>

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
    REQUIRE(s.titleFont()->height() == 18);
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

TEST_CASE("focused fullscreen covers the toolbar (chrome + bar gone)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFF1188FFu, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  server.focusViewForTest(v);
  server.toggleFullscreenForTest();
  for (int i = 0; i < 30; ++i) { c.flush(); server.dispatch(); c.pump(); }

  test::Frame f = test::captureFrame(server);
  REQUIRE(f.w == 1280u);
  REQUIRE(f.h == 720u);
  // The bottom-centre band is where the BottomCenter toolbar lives; under a
  // fullscreen view it must be the client colour, not bar chrome.
  auto pix = [&](int x, int y) { return f.pixels[static_cast<size_t>(y) * f.w + x] & 0x00FFFFFFu; };
  CHECK(pix(640, 710) == 0x1188FFu);
  CHECK(test::compareGolden(f, "tests/golden/v1-fullscreen.png", 2, 40));
}

TEST_CASE("unfocused fullscreen demotes: alt-tab preview of a normal window shows over it") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient fs(server.socketName(), 0xFF1188FFu, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *vfs = mapOne(server, fs);
  test::TestClient nm(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *vnm = mapOne(server, nm);
  vnm->setPosition(160, 120);

  server.focusViewForTest(vfs);
  server.toggleFullscreenForTest();          // vfs fullscreen + focused -> promoted
  for (int i = 0; i < 20; ++i) { fs.flush(); nm.flush(); server.dispatch(); fs.pump(); nm.pump(); }
  CHECK(server.viewLayerIsFullscreenForTest(vfs));

  // Alt-tab to the normal window: preview focuses vnm, so vfs loses focus and
  // demotes to layer_window; the raised preview must be visible above it.
  server.injectKeyForTest(XKB_KEY_Tab, WLR_MODIFIER_ALT, true);
  for (int i = 0; i < 20; ++i) { fs.flush(); nm.flush(); server.dispatch(); fs.pump(); nm.pump(); }
  CHECK_FALSE(server.viewLayerIsFullscreenForTest(vfs));   // demoted on unfocus
  server.injectKeyForTest(XKB_KEY_Alt_L, 0, false);        // commit the cycle
}
