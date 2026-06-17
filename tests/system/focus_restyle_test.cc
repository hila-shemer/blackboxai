// F1.3: focusing a window via titlebar click propagates to View::focused_ and
// causes the decorator to render the active look; blurring it renders the
// inactive look. Nonvisual — no golden compare.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>  // BTN_LEFT

using namespace bbai;

TEST_CASE("titlebar click propagates focus state to both views") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    REQUIRE_FALSE(server.socketName().empty());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
        server.dispatch();

    // Map two SSD clients.
    test::TestClient c1(server.socketName(), 0xFFFF0000u, 200, 150,
                        test::TestClient::Deco::RequestSSD);
    REQUIRE(c1.ok());

    auto v1Mapped = [&] {
        const auto &v = server.viewsForTest();
        return !v.empty() && v[0]->isMapped();
    };
    for (int i = 0; i < 500 && !v1Mapped(); ++i) {
        c1.flush(); server.dispatch(); c1.pump();
    }
    REQUIRE(v1Mapped());
    // Settle so the frame/decoration is fully laid out.
    for (int i = 0; i < 30; ++i) { c1.flush(); server.dispatch(); c1.pump(); }

    // v1 sits at its default position (160,120). Its titlebar is at global ~(260,130).
    View *v1 = server.viewsForTest()[0].get();

    // Map a second client and move it so its titlebar doesn't overlap v1's.
    test::TestClient c2(server.socketName(), 0xFF0000FFu, 200, 150,
                        test::TestClient::Deco::RequestSSD);
    REQUIRE(c2.ok());

    auto v2Mapped = [&] {
        const auto &v = server.viewsForTest();
        return v.size() >= 2 && v[1]->isMapped();
    };
    for (int i = 0; i < 500 && !v2Mapped(); ++i) {
        c2.flush(); server.dispatch(); c2.pump();
    }
    REQUIRE(v2Mapped());
    for (int i = 0; i < 30; ++i) { c2.flush(); server.dispatch(); c2.pump(); }

    View *v2 = server.viewsForTest()[1].get();
    // Move v2 so its titlebar is at a distinct vertical band (e.g. x=700 area).
    v2->setPosition(600, 120);
    // Pump so the reposition is reflected.
    for (int i = 0; i < 10; ++i) { c2.flush(); server.dispatch(); c2.pump(); }

    // v1 titlebar: centre of v1 title region, global (260,130).
    // v2 titlebar: centre of v2 title region, global (700,130).

    // Click v1's titlebar: press + release with no motion → focus, no move.
    server.injectPointerMotionForTest(260, 130);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
    for (int i = 0; i < 10; ++i) { c1.flush(); server.dispatch(); c1.pump(); }

    CHECK(v1->isFocused());
    CHECK(!v2->isFocused());
    CHECK(server.focusedViewForTest() == v1);

    // Click v2's titlebar.
    server.injectPointerMotionForTest(700, 130);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
    for (int i = 0; i < 10; ++i) { c2.flush(); server.dispatch(); c2.pump(); }

    CHECK(v2->isFocused());
    CHECK(!v1->isFocused());
    CHECK(server.focusedViewForTest() == v2);
}
