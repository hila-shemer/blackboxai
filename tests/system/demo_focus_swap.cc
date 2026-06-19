// Demo scenario: focus_swap
// Maps two SSD clients and demonstrates clicking titlebars to transfer focus.
// Asserts correct focus state at each step; dumps frames when BBAI_DEMO_DIR is
// set so tools/make-demos.sh can encode a demo video.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "DemoRecorder.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>

using namespace bbai;

TEST_CASE("demo: focus_swap — click titlebars to transfer focus between two windows") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  test::DemoRecorder rec("focus_swap");

  // Window A: red content at default position (160,120).
  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  REQUIRE(ca.ok());

  auto aMapped = [&] {
    const auto &v = server.viewsForTest();
    return !v.empty() && v[0]->isMapped();
  };
  for (int i = 0; i < 500 && !aMapped(); ++i) { ca.flush(); server.dispatch(); ca.pump(); }
  REQUIRE(aMapped());
  for (int i = 0; i < 30; ++i) { ca.flush(); server.dispatch(); ca.pump(); }

  // Window B: blue content — map then move below A.
  test::TestClient cb(server.socketName(), 0xFF0000FFu, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  REQUIRE(cb.ok());

  auto bMapped = [&] {
    const auto &v = server.viewsForTest();
    return v.size() >= 2 && v[1]->isMapped();
  };
  for (int i = 0; i < 500 && !bMapped(); ++i) { cb.flush(); server.dispatch(); cb.pump(); }
  REQUIRE(bMapped());
  for (int i = 0; i < 30; ++i) { ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump(); }

  View *A = server.viewsForTest()[0].get();
  View *B = server.viewsForTest()[1].get();

  // Move B below A so titlebars don't overlap.
  B->setPosition(160, 360);
  for (int i = 0; i < 10; ++i) { ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump(); }

  // Initial state: neither window explicitly focused yet — capture both visible.
  rec.shot(server);

  // --- Click A's titlebar (centre of title region, away from buttons) ---
  // A at (160,120); titlebar centre ~(261,131).
  server.injectPointerMotionForTest(261, 131);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
  for (int i = 0; i < 20; ++i) { ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump(); }

  CHECK(A->isFocused());
  CHECK_FALSE(B->isFocused());
  rec.shot(server, 3);

  // --- Click B's titlebar (B is at (160,360); titlebar centre ~(261,371)) ---
  server.injectPointerMotionForTest(261, 371);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
  for (int i = 0; i < 20; ++i) { ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump(); }

  CHECK(B->isFocused());
  CHECK_FALSE(A->isFocused());
  rec.shot(server, 3);
}
