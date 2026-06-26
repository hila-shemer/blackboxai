#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>   // BTN_LEFT / BTN_RIGHT

using namespace bbai;

static Server *boot() {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  auto *s = new Server(/*headless=*/true);
  REQUIRE(s->ok());
  for (int i = 0; i < 50 && s->activeSceneOutputForTest() == nullptr; ++i) s->dispatch();
  return s;
}

TEST_CASE("arm, drag, release: overlay appears then is gone, mode exits") {
  Server *s = boot();
  s->injectKeyForTest(XKB_KEY_F7, WLR_MODIFIER_LOGO, true);
  REQUIRE(s->screenshotActiveForTest());
  CHECK_FALSE(s->screenshotOverlayActiveForTest());     // armed, not yet dragging

  s->injectPointerMotionForTest(200, 200);
  s->injectPointerButtonForTest(BTN_LEFT, true);        // anchor A
  CHECK(s->screenshotOverlayActiveForTest());
  s->injectPointerMotionForTest(400, 350);              // drag
  CHECK(s->screenshotOverlayActiveForTest());
  s->injectPointerButtonForTest(BTN_LEFT, false);       // release B (real drag)
  CHECK_FALSE(s->screenshotActiveForTest());            // back to passthrough
  CHECK_FALSE(s->screenshotOverlayActiveForTest());     // overlay torn down
  delete s;
}

TEST_CASE("Escape cancels the select mode") {
  Server *s = boot();
  s->injectKeyForTest(XKB_KEY_F7, WLR_MODIFIER_LOGO, true);
  s->injectPointerMotionForTest(200, 200);
  s->injectPointerButtonForTest(BTN_LEFT, true);
  REQUIRE(s->screenshotOverlayActiveForTest());
  s->injectKeyForTest(XKB_KEY_Escape, 0, true);
  CHECK_FALSE(s->screenshotActiveForTest());
  CHECK_FALSE(s->screenshotOverlayActiveForTest());
  delete s;
}

TEST_CASE("right-click cancels the select mode") {
  Server *s = boot();
  s->injectKeyForTest(XKB_KEY_F7, WLR_MODIFIER_LOGO, true);
  s->injectPointerMotionForTest(200, 200);
  s->injectPointerButtonForTest(BTN_LEFT, true);
  s->injectPointerButtonForTest(BTN_RIGHT, true);       // cancel
  CHECK_FALSE(s->screenshotActiveForTest());
  CHECK_FALSE(s->screenshotOverlayActiveForTest());
  delete s;
}

TEST_CASE("a sub-pixel drag is a cancel, not a capture") {
  Server *s = boot();
  s->injectKeyForTest(XKB_KEY_F7, WLR_MODIFIER_LOGO, true);
  s->injectPointerMotionForTest(200, 200);
  s->injectPointerButtonForTest(BTN_LEFT, true);
  s->injectPointerMotionForTest(201, 201);              // < 4px
  s->injectPointerButtonForTest(BTN_LEFT, false);
  CHECK_FALSE(s->screenshotActiveForTest());            // exited, no capture (Task 7 asserts no selection)
  CHECK_FALSE(s->screenshotOverlayActiveForTest());
  delete s;
}

TEST_CASE("while selecting, pointer input does not reach a focused client") {
  Server *s = boot();
  test::TestClient c(s->socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  REQUIRE(c.ok());
  auto mapped = [&] { const auto &v = s->viewsForTest(); return !v.empty() && v[0]->isMapped(); };
  for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); s->dispatch(); c.pump(); }
  s->injectKeyForTest(XKB_KEY_F7, WLR_MODIFIER_LOGO, true);
  s->injectPointerMotionForTest(260, 130);              // over the client
  s->injectPointerButtonForTest(BTN_LEFT, true);
  for (int i = 0; i < 10; ++i) { c.flush(); s->dispatch(); c.pump(); }
  CHECK(c.pointerButtonEvents() == 0);                  // modal: client saw nothing
  delete s;
}
