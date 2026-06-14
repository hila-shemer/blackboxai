// T5: xdg-decoration policy — the compositor requests server-side decorations
// (draws the Blackbox frame) by default, but honors a client that insists on
// client-side decorations (draws no chrome, still owns the View/geometry).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>

using namespace bbai;

// Matches wlr_xdg_toplevel_decoration_v1_mode.
static constexpr int kModeClientSide = 1;
static constexpr int kModeServerSide = 2;

namespace {
  // Boot a headless Server, map a green client requesting `deco`, and settle the
  // decoration handshake. Returns the mapped View (or null on failure).
  const View *bootAndMap(Server &server, test::TestClient &client) {
    if (!server.ok()) return nullptr;
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
      server.dispatch();
    if (!client.ok()) return nullptr;
    auto mapped = [&] {
      const auto &v = server.viewsForTest();
      return !v.empty() && v[0]->isMapped();
    };
    for (int i = 0; i < 500 && !mapped(); ++i) {
      client.flush(); server.dispatch(); client.pump();
    }
    if (!mapped()) return nullptr;
    // Let the client ack the decoration configure so current.mode settles.
    for (int i = 0; i < 50; ++i) { client.flush(); server.dispatch(); client.pump(); }
    return server.viewsForTest()[0].get();
  }
}

TEST_CASE("a client requesting server-side decorations gets the Blackbox frame") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  test::TestClient client(server.socketName(), 0xFF00FF00u, 200, 150,
                          test::TestClient::Deco::RequestSSD);
  const View *v = bootAndMap(server, client);
  REQUIRE(v != nullptr);

  CHECK(v->drawsFrame());
  CHECK(v->decorationMode() == kModeServerSide);

  test::Frame f = test::captureFrame(server);
  auto rgb = [&](int x, int y) { return f.pixels[static_cast<size_t>(y) * f.w + x] & 0x00FFFFFFu; };
  auto isGrey = [&](uint32_t c) {
    return ((c >> 16) & 0xFF) == ((c >> 8) & 0xFF) && ((c >> 8) & 0xFF) == (c & 0xFF);
  };
  // SSD: grey titlebar decoration above the green content (content at (161,143)).
  CHECK(isGrey(rgb(170, 130)));
  CHECK(rgb(261, 218) == 0x0000FF00u);
}

TEST_CASE("a client insisting on client-side decorations is honored — no chrome") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  test::TestClient client(server.socketName(), 0xFF00FF00u, 200, 150,
                          test::TestClient::Deco::RequestCSD);
  const View *v = bootAndMap(server, client);
  REQUIRE(v != nullptr);

  CHECK_FALSE(v->drawsFrame());
  CHECK(v->decorationMode() == kModeClientSide);

  test::Frame f = test::captureFrame(server);
  auto rgb = [&](int x, int y) { return f.pixels[static_cast<size_t>(y) * f.w + x] & 0x00FFFFFFu; };
  // No chrome: the client surface sits at the View origin (160,120), so the
  // pixels that would be titlebar under SSD are the green client content.
  CHECK(rgb(161, 121) == 0x0000FF00u);
  CHECK(rgb(260, 130) == 0x0000FF00u);
  // Outside the window is still the gradient.
  CHECK(rgb(40, 40) != 0x0000FF00u);
}
