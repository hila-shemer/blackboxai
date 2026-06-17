// F4.6: golden of a maximized SSD window filling the work area above the
// toolbar. Maps one red SSD client, clicks the maximize button, pumps until
// the client commits the new buffer, then compares the composited frame to a
// golden PNG.
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

TEST_CASE("maximized SSD window fills the work area — golden") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
        server.dispatch();

    // Red SSD client, 200x150 content.
    test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                       test::TestClient::Deco::RequestSSD);
    REQUIRE(c.ok());

    // Pump until mapped + settle decorations.
    auto mapped = [&] {
        const auto &v = server.viewsForTest();
        return !v.empty() && v[0]->isMapped();
    };
    for (int i = 0; i < 500 && !mapped(); ++i) {
        c.flush(); server.dispatch(); c.pump();
    }
    REQUIRE(mapped());
    for (int i = 0; i < 30; ++i) { c.flush(); server.dispatch(); c.pump(); }

    const auto &views = server.viewsForTest();
    REQUIRE(views.size() >= 1);
    REQUIRE(views[0]->isMapped());

    View *v = views[0].get();

    // Focus the view via titlebar click (middle of title, away from buttons).
    server.injectPointerMotionForTest(v->x() + 100, v->y() + 10);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
    REQUIRE(server.focusedViewForTest() == v);

    // Compute the maximize-button global centre.
    const frame::Rect mb = frame::maximizeButton(v->contentWidth(), v->contentHeight());
    const int mx = v->x() + mb.x + frame::kButtonWidth / 2;
    const int my = v->y() + mb.y + frame::kButtonWidth / 2;

    server.injectPointerMotionForTest(mx, my);
    REQUIRE(server.partAtForTest(mx, my) == Part::MaximizeButton);

    // Press + release inside the maximize button.
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);

    // Pump generously so the client commits the new maximized buffer and the
    // compositor rebuilds scene nodes for the new geometry.
    for (int i = 0; i < 500; ++i) {
        c.flush(); server.dispatch(); c.pump();
        if (v->isMaximized() && frame::frameWidth(v->contentWidth()) == kOutputW)
            break;
    }
    // Extra settle cycles for decoration relayout.
    for (int i = 0; i < 30; ++i) { c.flush(); server.dispatch(); c.pump(); }

    // Sanity checks before the golden.
    CHECK(v->isMaximized());
    CHECK(v->x() == 0);
    CHECK(v->y() == 0);

    CHECK(test::compareGolden(test::captureFrame(server), "tests/golden/m4-maximized.png", 2, 80));
}
