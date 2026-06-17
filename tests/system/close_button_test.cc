// F4.4: Clicking the close title-bar button sends an xdg_toplevel.close request.
// Nonvisual — no golden compare.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"
#include "Frame.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>  // BTN_LEFT

using namespace bbai;

// Map one SSD client and settle decorations.
static void mapOne(Server &server, test::TestClient &c, int iterations = 500) {
    auto mapped = [&] {
        const auto &v = server.viewsForTest();
        return !v.empty() && v[0]->isMapped();
    };
    for (int i = 0; i < iterations && !mapped(); ++i) {
        c.flush(); server.dispatch(); c.pump();
    }
    // Extra settle cycles so SSD decorations commit.
    for (int i = 0; i < 30; ++i) { c.flush(); server.dispatch(); c.pump(); }
}

// Pump a few extra rounds so in-flight Wayland events reach the client.
static void settle(Server &server, test::TestClient &c, int n = 10) {
    for (int i = 0; i < n; ++i) { c.flush(); server.dispatch(); c.pump(); }
}

TEST_CASE("close button press+release-inside sends xdg close request") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
        server.dispatch();

    test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                       test::TestClient::Deco::RequestSSD);
    REQUIRE(c.ok());
    mapOne(server, c);

    const auto &views = server.viewsForTest();
    REQUIRE(views.size() >= 1);
    REQUIRE(views[0]->isMapped());

    View *v = views[0].get();

    // Focus the view via titlebar click (middle of title, away from buttons).
    // Default position is (160,120); title area is at y ~ [120,143), use (260,130).
    server.injectPointerMotionForTest(260, 130);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
    REQUIRE(server.focusedViewForTest() == v);

    // Compute the close-button global centre.
    // closeButton(W,H) = { frameWidth(W) - kButtonStep, kTitleMargin, kButtonWidth, kButtonWidth }
    // i.e. rightmost button in the title bar.
    const frame::Rect cb = frame::closeButton(v->contentWidth(), v->contentHeight());
    const int cx = v->x() + cb.x + frame::kButtonWidth / 2;
    const int cy = v->y() + cb.y + frame::kButtonWidth / 2;

    server.injectPointerMotionForTest(cx, cy);
    REQUIRE(server.partAtForTest(cx, cy) == Part::CloseButton);

    // Press then release inside the close button.
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);

    // Pump so the xdg_toplevel.close event travels to the client.
    settle(server, c);

    CHECK(c.gotCloseRequest());
}

TEST_CASE("close button: release outside the button is a no-op") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
        server.dispatch();

    test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                       test::TestClient::Deco::RequestSSD);
    REQUIRE(c.ok());
    mapOne(server, c);

    const auto &views = server.viewsForTest();
    REQUIRE(views.size() >= 1);
    REQUIRE(views[0]->isMapped());

    View *v = views[0].get();

    // Compute close-button centre.
    const frame::Rect cb = frame::closeButton(v->contentWidth(), v->contentHeight());
    const int cx = v->x() + cb.x + frame::kButtonWidth / 2;
    const int cy = v->y() + cb.y + frame::kButtonWidth / 2;

    server.injectPointerMotionForTest(cx, cy);
    REQUIRE(server.partAtForTest(cx, cy) == Part::CloseButton);

    // Press inside, then drift the cursor far away.
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
    server.injectPointerMotionForTest(800, 600);

    // Release outside — must NOT send close.
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);

    settle(server, c);

    CHECK_FALSE(c.gotCloseRequest());
}
