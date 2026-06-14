// T8: dragging a bottom resize grip resizes the client (set_size -> client acks
// -> recommits at the new size) and the decoration frame re-lays-out around it.
// Bottom-right grows the right/bottom edges; bottom-left anchors the right edge
// and moves the top-left x.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>  // BTN_LEFT

using namespace bbai;

namespace {
  const View *mapClient(Server &server, test::TestClient &client) {
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
      server.dispatch();
    auto mapped = [&] {
      const auto &v = server.viewsForTest();
      return !v.empty() && v[0]->isMapped();
    };
    for (int i = 0; i < 500 && !mapped(); ++i) { client.flush(); server.dispatch(); client.pump(); }
    if (!mapped()) return nullptr;
    for (int i = 0; i < 30; ++i) { client.flush(); server.dispatch(); client.pump(); }
    return server.viewsForTest()[0].get();
  }
  // Pump until the client has acked the requested content size.
  void settle(Server &server, test::TestClient &client, const View *v, int w, int h) {
    for (int i = 0; i < 120 &&
         (v->toplevel()->current.width != w || v->toplevel()->current.height != h); ++i) {
      client.flush(); server.dispatch(); client.pump();
    }
    for (int i = 0; i < 10; ++i) { client.flush(); server.dispatch(); client.pump(); }
  }
}

TEST_CASE("dragging the bottom-right grip grows the window and re-lays-out the frame") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  test::TestClient client(server.socketName(), 0xFFFF0000u, 200, 150,
                          test::TestClient::Deco::RequestSSD);
  REQUIRE(client.ok());
  const View *v = mapClient(server, client);
  REQUIRE(v != nullptr);

  // Press the bottom-right grip (abs ~(350,295)), drag (+40,+20), release.
  server.injectPointerMotionForTest(350, 295);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
  server.injectPointerMotionForTest(350 + 40, 295 + 20);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
  settle(server, client, v, 240, 170);

  CHECK(v->x() == 160);
  CHECK(v->y() == 120);
  CHECK(v->contentWidth() == 240);
  CHECK(v->contentHeight() == 170);
  CHECK(v->toplevel()->current.width == 240);
  CHECK(v->toplevel()->current.height == 170);

  test::Frame f = test::captureFrame(server);
  auto rgb    = [&](int x, int y) { return f.pixels[static_cast<size_t>(y) * f.w + x] & 0x00FFFFFFu; };
  auto isGrey = [&](uint32_t c) {
    return ((c >> 16) & 0xFF) == ((c >> 8) & 0xFF) && ((c >> 8) & 0xFF) == (c & 0xFF);
  };
  // Content now 240x170 at (161,143): centre is red, and the right border moved
  // out to x=401 (the old border column at x=361 is now content).
  CHECK(rgb(281, 228) == 0x00FF0000u);
  CHECK(rgb(361, 200) == 0x00FF0000u);
  CHECK(rgb(401, 200) == 0x00303030u);
  // Handle re-laid-out lower (the old handle row at y=295 is now content).
  CHECK(isGrey(rgb(260, 315)));
  CHECK(rgb(260, 295) == 0x00FF0000u);

  // See frame_test.cc: tolerance pins the layout; the small budget absorbs only
  // residual glyph-edge jitter across FreeType versions (hinting disabled).
  CHECK(test::compareGolden(f, "tests/golden/m3-resize.png", 2, 40));
}

TEST_CASE("dragging the bottom-left grip anchors the right edge and moves the left") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  test::TestClient client(server.socketName(), 0xFFFF0000u, 200, 150,
                          test::TestClient::Deco::RequestSSD);
  REQUIRE(client.ok());
  const View *v = mapClient(server, client);
  REQUIRE(v != nullptr);

  // Press the bottom-left grip (abs ~(170,295)), drag (-30,+20), release.
  server.injectPointerMotionForTest(170, 295);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
  server.injectPointerMotionForTest(170 - 30, 295 + 20);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
  settle(server, client, v, 230, 170);

  // Top-left moved left/unchanged-y; width grew by the drag, height grew.
  CHECK(v->x() == 130);
  CHECK(v->y() == 120);
  CHECK(v->contentWidth() == 230);
  CHECK(v->contentHeight() == 170);

  test::Frame f = test::captureFrame(server);
  auto rgb = [&](int x, int y) { return f.pixels[static_cast<size_t>(y) * f.w + x] & 0x00FFFFFFu; };
  // The right edge stayed anchored at x=361 (border still there); the left
  // border moved out to x=130; the old left-border column at x=160 is now content.
  CHECK(rgb(361, 200) == 0x00303030u);
  CHECK(rgb(130, 200) == 0x00303030u);
  CHECK(rgb(160, 200) == 0x00FF0000u);
}

TEST_CASE("over-shrinking the bottom-left grip keeps the right edge anchored (no march-off)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  test::TestClient client(server.socketName(), 0xFFFF0000u, 200, 150,
                          test::TestClient::Deco::RequestSSD);
  REQUIRE(client.ok());
  const View *v = mapClient(server, client);
  REQUIRE(v != nullptr);

  // Drag the bottom-left grip far past the window's own width (so width clamps
  // to the 1px minimum). The anchored right edge was at x = 160 + 200 = 360, so
  // the top-left must end at 359 — NOT march off to 160 + 250.
  server.injectPointerMotionForTest(170, 295);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
  server.injectPointerMotionForTest(170 + 250, 295 + 20);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);

  // resizeTo sets the View geometry synchronously (no client ack needed here).
  CHECK(v->contentWidth() == 1);
  CHECK(v->x() == 359);          // right - clamped_w = 360 - 1
  CHECK(v->y() == 120);          // top edge unchanged (BOTTOM grip)
}
