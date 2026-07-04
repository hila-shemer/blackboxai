// tests/system/gray_window_test.cc
// Gray is the wildcard-and-parentrelative grammar: *font, toolbar*textColor,
// PR window labels/buttons. A mapped SSD window under Gray proves the whole
// chain - and the PR label is literally the titlebar's pixels, which we can
// assert directly (same column, inside vs outside the label rect).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"
#include "Frame.hh"
#include "Style.hh"

#include <cstdlib>

using namespace bbai;

TEST_CASE("a Gray-styled SSD window: wildcards + PR label pixel-copy") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true, "tests/fixtures/gray.blackboxrc");
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();
  REQUIRE(server.currentStyle()->sourcePath() == "data/styles/Gray");

  test::TestClient client(server.socketName(), 0xFFFF0000u, 200, 150);
  REQUIRE(client.ok());
  auto mapped = [&] {
    const auto &v = server.viewsForTest();
    return !v.empty() && v[0]->isMapped();
  };
  for (int i = 0; i < 500 && !mapped(); ++i) {
    client.flush(); server.dispatch(); client.pump();
  }
  REQUIRE(mapped());
  for (int i = 0; i < 10; ++i) { client.flush(); server.dispatch(); client.pump(); }

  const View *v = server.viewsForTest()[0].get();
  const frame::FrameMetrics &m = server.currentStyle()->frameMetrics();
  CHECK(m.handleHeight == 10);   // Gray's window.handleHeight 8 + *borderWidth 1 folded twice

  test::Frame f = test::captureFrame(server);
  auto rgb = [&](int x, int y) { return f.pixels[static_cast<size_t>(y) * f.w + x] & 0xFFFFFFu; };

  // PR label == parent pixel copy: the label region is NOT a separately
  // rendered block - it CONTINUES the titlebar texture across its edges.
  // Gray's title is a diagonal gradient, so adjacent pixels differ by a hair;
  // probe the seam with a small epsilon. If PR were broken, the label would
  // render its own flat-solid fallback (*backgroundColor rgb:dd/dd/d7 = 221)
  // - a jump of ~100 per channel, orders beyond the epsilon.
  const frame::Rect lr = frame::label(200, 150, m);
  const int fx = v->x(), fy = v->y();
  const int y_seam = fy + lr.y + 1;   // top row of the label, no glyph ink there
  auto near = [&](uint32_t a, uint32_t b) {
    auto ch = [](uint32_t c, int s) { return int((c >> s) & 0xFF); };
    return std::abs(ch(a,16)-ch(b,16)) <= 3 && std::abs(ch(a,8)-ch(b,8)) <= 3
        && std::abs(ch(a,0)-ch(b,0)) <= 3;
  };
  CHECK(near(rgb(fx + lr.x, y_seam), rgb(fx + lr.x - 1, y_seam)));
  CHECK(near(rgb(fx + lr.x + lr.w - 1, y_seam), rgb(fx + lr.x + lr.w, y_seam)));

  CHECK(test::compareGolden(f, "tests/golden/v1-gray-window.png", 2, 40));
}
