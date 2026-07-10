// Dogfooding finding 2026-07-10: no paste worked anywhere - the compositor never
// answered a client's request_set_selection (every copy was silently dropped)
// and the primary-selection global did not exist (middle-click had no protocol).
// A client copy must land in the seat for both selection kinds. The serials are
// real ones the compositor handed the client (pointer enter/button) - wlroots
// rejects a serial it never gave.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>  // BTN_LEFT

using namespace bbai;

TEST_CASE("a client copy becomes the seat selection - clipboard and primary") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
        server.dispatch();

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
    server.viewsForTest()[0]->setPosition(160, 120);
    for (int i = 0; i < 40; ++i) { client.flush(); server.dispatch(); client.pump(); }

    // Click into the client CONTENT (not chrome) so the client is handed real
    // input serials to copy with.
    server.injectPointerMotionForTest(261, 218);
    server.injectPointerButtonForTest(BTN_LEFT, true);
    server.injectPointerButtonForTest(BTN_LEFT, false);
    auto clicked = [&] { return client.pointerButtonEvents() >= 2; };
    for (int i = 0; i < 200 && !clicked(); ++i) {
        client.flush();
        server.dispatch();
        client.pump();
    }
    REQUIRE(clicked());

    REQUIRE(server.seatSelectionSourceForTest() == nullptr);
    REQUIRE(client.copyToClipboard());
    for (int i = 0; i < 100 && !server.seatSelectionSourceForTest(); ++i) {
        client.flush();
        server.dispatch();
        client.pump();
    }
    CHECK(server.seatSelectionSourceForTest() != nullptr);

    REQUIRE(server.primarySelectionSourceForTest() == nullptr);
    REQUIRE(client.copyToPrimary());
    for (int i = 0; i < 100 && !server.primarySelectionSourceForTest(); ++i) {
        client.flush();
        server.dispatch();
        client.pump();
    }
    CHECK(server.primarySelectionSourceForTest() != nullptr);
}
