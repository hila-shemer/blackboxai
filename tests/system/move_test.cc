// T7: dragging the titlebar moves the window and its whole decoration frame.
// Injects a titlebar press, a drag, and a release through the test injection
// API (the same grab state machine the real cursor drives), then asserts the
// new position + a golden.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>  // BTN_LEFT

using namespace bbai;

TEST_CASE("dragging the titlebar moves the window and its frame") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  test::TestClient client(server.socketName(), 0xFFFF0000u, 200, 150,
                          test::TestClient::Deco::RequestSSD);
  REQUIRE(client.ok());
  auto mapped = [&] {
    const auto &v = server.viewsForTest();
    return !v.empty() && v[0]->isMapped();
  };
  for (int i = 0; i < 500 && !mapped(); ++i) { client.flush(); server.dispatch(); client.pump(); }
  REQUIRE(mapped());
  for (int i = 0; i < 30; ++i) { client.flush(); server.dispatch(); client.pump(); }

  View *v = server.viewsForTest()[0].get();
  REQUIRE(v->x() == 160);
  REQUIRE(v->y() == 120);

  // Press on the titlebar at (260,130), drag by (+40,+30), release.
  server.injectPointerMotionForTest(260, 130);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
  server.injectPointerMotionForTest(260 + 40, 130 + 30);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);

  // The window + frame moved by the drag delta.
  CHECK(v->x() == 200);
  CHECK(v->y() == 150);

  test::Frame f = test::captureFrame(server);
  auto rgb    = [&](int x, int y) { return f.pixels[static_cast<size_t>(y) * f.w + x] & 0x00FFFFFFu; };
  auto isGrey = [&](uint32_t c) {
    return ((c >> 16) & 0xFF) == ((c >> 8) & 0xFF) && ((c >> 8) & 0xFF) == (c & 0xFF);
  };

  // Frame now at (200,150): the iconify button (grey) sits in the new titlebar,
  // and the red content centre is at (301,248).
  CHECK(isGrey(rgb(210, 158)));
  CHECK(rgb(301, 248) == 0x00FF0000u);
  // The old titlebar location is now bare gradient desktop (not grey chrome).
  CHECK_FALSE(isGrey(rgb(260, 130)));

  // See frame_test.cc: tolerance pins the layout; the small budget absorbs only
  // residual glyph-edge jitter across FreeType versions (hinting disabled).
  CHECK(test::compareGolden(f, "tests/golden/m3-move.png", 2, 40));
}
