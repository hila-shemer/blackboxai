#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>

using namespace bbai;

TEST_CASE("a real client toplevel is composited over the gradient background") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    REQUIRE_FALSE(server.socketName().empty());

    // Let the output come up.
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
        server.dispatch();

    // Opaque red, 200x150 (matches View's fixed configure/placement at 160,120).
    test::TestClient client(server.socketName(), 0xFFFF0000u, 200, 150);
    REQUIRE(client.ok());

    auto mapped = [&] {
        const auto &v = server.viewsForTest();
        return !v.empty() && v[0]->isMapped();
    };
    for (int i = 0; i < 500 && !mapped(); ++i) {
        client.flush();
        server.dispatch();
        client.pump();
    }
    REQUIRE(mapped());

    test::Frame f = test::captureFrame(server);
    REQUIRE(f.w == 1280u);
    REQUIRE(f.h == 720u);

    auto rgb = [&](int x, int y) {
        return f.pixels[static_cast<size_t>(y) * f.w + x] & 0x00FFFFFFu;
    };
    // Window spans x[160,360) y[120,270); its centre must be the client's red.
    CHECK(rgb(260, 195) == 0x00FF0000u);
    // Just inside the top-left corner is red too.
    CHECK(rgb(162, 122) == 0x00FF0000u);
    // A point well outside the window is the gradient, not red.
    CHECK(rgb(40, 40) != 0x00FF0000u);

    CHECK(test::compareGolden(f, "tests/golden/m2-client-red-window.png", 2, 0));

    // Closing the window must remove the View (exercises unmap/destroy/removeView).
    client.closeWindow();
    auto gone = [&] { return server.viewsForTest().empty(); };
    for (int i = 0; i < 200 && !gone(); ++i) {
        client.flush();
        server.dispatch();
        client.pump();
    }
    CHECK(gone());
}
