// A real client toplevel maps, is composited (now with its M3 decoration frame),
// and is cleanly torn down when closed. The pixel-exact frame appearance is
// owned by frame_test.cc + the m3-frame-ssd golden; this test focuses on the
// lifecycle: content visible at the decorated offset, then close -> unmap ->
// destroy -> removeView. Runs under the isolated fontconfig (decorated window
// carries title text).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>

using namespace bbai;

TEST_CASE("a real client maps decorated and tears down cleanly on close") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    REQUIRE_FALSE(server.socketName().empty());

    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
        server.dispatch();

    // Opaque red, 200x150 content (View configures + places it at (160,120)).
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
    // Decorated: the red client content sits at (161,143); its centre is red.
    CHECK(rgb(261, 218) == 0x00FF0000u);
    // The old undecorated top-left (162,122) is now titlebar, not red.
    CHECK(rgb(162, 122) != 0x00FF0000u);
    // A point well outside the window is the gradient, not red.
    CHECK(rgb(40, 40) != 0x00FF0000u);

    // Closing the window must remove the View (unmap/destroy/removeView path).
    client.closeWindow();
    auto gone = [&] { return server.viewsForTest().empty(); };
    for (int i = 0; i < 200 && !gone(); ++i) {
        client.flush();
        server.dispatch();
        client.pump();
    }
    CHECK(gone());
}
