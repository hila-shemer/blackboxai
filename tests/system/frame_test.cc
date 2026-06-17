// T4: a mapped SSD client gets a pixel-accurate Blackbox decoration frame
// (titlebar + label text + handle + grips + side borders + three buttons)
// composited over the M1 gradient. Runs under the isolated fontconfig (set by
// meson) so the rendered title text is deterministic and the golden stays strict.
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

TEST_CASE("a mapped SSD client is wrapped in a Blackbox frame") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    REQUIRE_FALSE(server.socketName().empty());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
        server.dispatch();

    // Opaque red 200x150 content (matches View's fixed configure at (160,120)).
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

    // View geometry: content size fixed, frame placed at (160,120).
    const View *v = server.viewsForTest()[0].get();
    CHECK(v->contentWidth()  == 200);
    CHECK(v->contentHeight() == 150);
    CHECK(v->x() == 160);
    CHECK(v->y() == 120);
    // Frame is content + decorations: 202 x 179 (per frame::frameWidth/Height).
    CHECK(frame::frameWidth(200)  == 202);
    CHECK(frame::frameHeight(150) == 179);

    // Focus the window via a titlebar click (press + release, no drag) so the
    // canonical golden captures the ACTIVE decoration look.
    server.injectPointerMotionForTest(260, 130);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
    // Pump so the setFocused relayout completes before we capture.
    for (int i = 0; i < 10; ++i) { client.flush(); server.dispatch(); client.pump(); }

    test::Frame f = test::captureFrame(server);
    REQUIRE(f.w == 1280u);
    REQUIRE(f.h == 720u);

    auto rgb    = [&](int x, int y) { return f.pixels[static_cast<size_t>(y) * f.w + x] & 0x00FFFFFFu; };
    auto isGrey = [&](uint32_t c) {
        return ((c >> 16) & 0xFF) == ((c >> 8) & 0xFF) && ((c >> 8) & 0xFF) == (c & 0xFF);
    };

    // Frame spans x[160,362) y[120,299); content at (161,143) size 200x150.
    // Client content centre is red.
    CHECK(rgb(261, 218) == 0x00FF0000u);
    // Titlebar covers the top: a button pixel is grey, not red, not the gradient.
    CHECK(isGrey(rgb(170, 130)));
    // Handle middle (below the client, between the grips) is grey decoration.
    CHECK(isGrey(rgb(260, 295)));
    // Left border column beside the client is the dark border color (#303030).
    CHECK(rgb(160, 200) == 0x00303030u);

    // Frame extent: right border at x=361 is decoration; x=362 is the gradient.
    CHECK(rgb(361, 200) == 0x00303030u);
    CHECK(rgb(362, 200) != 0x00303030u);
    // Bottom handle row at y=298 is grey; y=299 is the gradient again.
    CHECK(isGrey(rgb(260, 298)));

    // The title label ("bbai-test") rendered actual dark glyph pixels in the
    // label region (abs x[184,317) y[124,139)).
    int darkInLabel = 0;
    for (int y = 124; y < 139; ++y)
        for (int x = 184; x < 317; ++x) {
            uint32_t c = rgb(x, y);
            if (int((c >> 16) & 0xFF) + int((c >> 8) & 0xFF) + int(c & 0xFF) < 150) ++darkInLabel;
        }
    CHECK(darkInLabel > 5);

    // A point well outside the window is the gradient, not red.
    CHECK(rgb(40, 40) != 0x00FF0000u);

    // tolerance 2 keeps the frame layout pinned; the small budget absorbs only
    // residual glyph-edge jitter if CI's FreeType differs from the bless host
    // (hinting is disabled in tests/fixtures/fonts.conf to minimize that). A real
    // layout regression moves whole elements (hundreds+ of pixels) and still fails.
    CHECK(test::compareGolden(f, "tests/golden/m3-frame-ssd.png", 2, 40));
}
