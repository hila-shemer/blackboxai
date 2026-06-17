// F1.4: two-window focus-swap golden. Maps two SSD clients with non-overlapping
// titlebars, focuses window A via a titlebar click, and proves the two titlebars
// render visibly differently (active vs inactive palette). A pixel-accurate
// golden locks the frame layout.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>  // BTN_LEFT

using namespace bbai;

TEST_CASE("two-window focus-swap: focused titlebar differs from unfocused") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    REQUIRE_FALSE(server.socketName().empty());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
        server.dispatch();

    // Window A: red content, default position (160,120), content 200x150.
    // Titlebar covers global y[120,143), titlebar centre ≈ (261,131).
    test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                        test::TestClient::Deco::RequestSSD);
    REQUIRE(ca.ok());

    auto vaMapped = [&] {
        const auto &v = server.viewsForTest();
        return !v.empty() && v[0]->isMapped();
    };
    for (int i = 0; i < 500 && !vaMapped(); ++i) {
        ca.flush(); server.dispatch(); ca.pump();
    }
    REQUIRE(vaMapped());
    for (int i = 0; i < 30; ++i) { ca.flush(); server.dispatch(); ca.pump(); }

    View *va = server.viewsForTest()[0].get();
    CHECK(va->x() == 160);
    CHECK(va->y() == 120);

    // Window B: blue content, moved to (160,360).
    // Titlebar covers global y[360,383), titlebar centre ≈ (261,371).
    test::TestClient cb(server.socketName(), 0xFF0000FFu, 200, 150,
                        test::TestClient::Deco::RequestSSD);
    REQUIRE(cb.ok());

    auto vbMapped = [&] {
        const auto &v = server.viewsForTest();
        return v.size() >= 2 && v[1]->isMapped();
    };
    for (int i = 0; i < 500 && !vbMapped(); ++i) {
        cb.flush(); server.dispatch(); cb.pump();
    }
    REQUIRE(vbMapped());
    for (int i = 0; i < 30; ++i) { cb.flush(); server.dispatch(); cb.pump(); }

    View *vb = server.viewsForTest()[1].get();
    vb->setPosition(160, 360);
    for (int i = 0; i < 10; ++i) { cb.flush(); server.dispatch(); cb.pump(); }

    // Focus window A via a titlebar click (press + release, no drag).
    // A's titlebar centre in global coords: x=261, y=131.
    server.injectPointerMotionForTest(261, 131);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
    for (int i = 0; i < 20; ++i) {
        ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump();
    }

    // Confirm focus state.
    REQUIRE(va->isFocused());
    REQUIRE(!vb->isFocused());
    REQUIRE(server.focusedViewForTest() == va);

    test::Frame f = test::captureFrame(server);
    REQUIRE(f.w == 1280u);
    REQUIRE(f.h == 720u);

    // Sample a pixel from the middle of each titlebar (away from buttons/label).
    // A's titlebar: global (220, 131); B's titlebar: global (220, 371).
    // Both are in the gradient region of the titlebar, far from any button.
    auto pixel = [&](int x, int y) {
        return f.pixels[static_cast<size_t>(y) * f.w + x] & 0x00FFFFFFu;
    };

    uint32_t focused_tb   = pixel(220, 131);  // window A titlebar (focused, active palette)
    uint32_t unfocused_tb = pixel(220, 371);  // window B titlebar (unfocused, inactive palette)

    // The active palette (#c0c0c0 → #808080) is brighter than the inactive
    // (#909090 → #606060). The two pixels must differ.
    CHECK(focused_tb != unfocused_tb);

    // The focused titlebar should be lighter (higher R channel) than the unfocused.
    uint32_t focused_r   = (focused_tb   >> 16) & 0xFF;
    uint32_t unfocused_r = (unfocused_tb >> 16) & 0xFF;
    CHECK(focused_r > unfocused_r);

    CHECK(test::compareGolden(f, "tests/golden/m4-focus-swap.png", 2, 40));
}
