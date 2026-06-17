// F4.5: Clicking the maximize title-bar button toggles maximize: the window
// frame fills the work area (output minus the toolbar), saving the
// pre-maximize geometry; clicking again restores it exactly.
// Nonvisual — no golden compare.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"
#include "Frame.hh"
#include "Toolbar.geom.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>  // BTN_LEFT

using namespace bbai;

// Headless output is fixed at 1280x720.
static constexpr int kOutputW = 1280;
static constexpr int kOutputH = 720;
// Work area height: output minus the toolbar.
static constexpr int kWorkH   = kOutputH - toolbar::kBarHeight;

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

TEST_CASE("maximize button toggles maximize and restores geometry") {
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

    // Record the pre-maximize geometry.
    const int x0 = v->x();
    const int y0 = v->y();
    const int w0 = v->contentWidth();
    const int h0 = v->contentHeight();

    // Focus the view via titlebar click (middle of title, away from buttons).
    server.injectPointerMotionForTest(v->x() + 100, v->y() + 10);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
    REQUIRE(server.focusedViewForTest() == v);

    // Compute the maximize-button global centre.
    // maximizeButton(W,H) = { frameWidth(W) - 2*kButtonStep, kTitleMargin, kButtonWidth, kButtonWidth }
    const frame::Rect mb = frame::maximizeButton(v->contentWidth(), v->contentHeight());
    const int mx = v->x() + mb.x + frame::kButtonWidth / 2;
    const int my = v->y() + mb.y + frame::kButtonWidth / 2;

    server.injectPointerMotionForTest(mx, my);
    REQUIRE(server.partAtForTest(mx, my) == Part::MaximizeButton);

    // Press + release inside the maximize button.
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);

    // resizeTo sets cw/ch synchronously; verify the maximized geometry.
    CHECK(v->isMaximized());
    CHECK(v->x() == 0);
    CHECK(v->y() == 0);
    // Frame must fill output width.
    CHECK(frame::frameWidth(v->contentWidth()) == kOutputW);
    // Frame bottom must touch the top of the toolbar.
    CHECK(v->y() + frame::frameHeight(v->contentHeight()) == kWorkH);

    // Pump so the client commits the new buffer size, which triggers relayout()
    // and rebuilds the decoration scene nodes for the maximized geometry.
    for (int i = 0; i < 30; ++i) { c.flush(); server.dispatch(); c.pump(); }

    // Click maximize again to restore.
    // The button is now at the new (maximized) position.
    const frame::Rect mb2 = frame::maximizeButton(v->contentWidth(), v->contentHeight());
    const int mx2 = v->x() + mb2.x + frame::kButtonWidth / 2;
    const int my2 = v->y() + mb2.y + frame::kButtonWidth / 2;

    server.injectPointerMotionForTest(mx2, my2);
    REQUIRE(server.partAtForTest(mx2, my2) == Part::MaximizeButton);

    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);

    // Geometry must be exactly restored.
    CHECK_FALSE(v->isMaximized());
    CHECK(v->x() == x0);
    CHECK(v->y() == y0);
    CHECK(v->contentWidth()  == w0);
    CHECK(v->contentHeight() == h0);
}

TEST_CASE("setMaximized is idempotent") {
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

    const int x0 = v->x();
    const int y0 = v->y();
    const int w0 = v->contentWidth();
    const int h0 = v->contentHeight();

    v->setMaximized(true, kOutputW, kWorkH);
    v->setMaximized(true, kOutputW, kWorkH);  // second call is a no-op
    CHECK(v->isMaximized());
    CHECK(frame::frameWidth(v->contentWidth()) == kOutputW);

    v->setMaximized(false, kOutputW, kWorkH);
    v->setMaximized(false, kOutputW, kWorkH);  // second call is a no-op
    CHECK_FALSE(v->isMaximized());
    CHECK(v->x() == x0);
    CHECK(v->y() == y0);
    CHECK(v->contentWidth()  == w0);
    CHECK(v->contentHeight() == h0);
}
