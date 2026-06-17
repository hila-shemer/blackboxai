// F1.2: View carries focus state; setFocused/isFocused are idempotent and
// trigger a relayout only for mapped SSD windows. Nonvisual — no golden compare.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>

using namespace bbai;

TEST_CASE("View focus state: get/set + idempotent + relayout guard") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
        server.dispatch();

    // Map one SSD client (default Deco::None -> compositor policy -> SSD).
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

    View *v = server.viewsForTest()[0].get();

    // Unfocused by default.
    CHECK(v->isFocused() == false);

    // setFocused(true) stores the state.
    v->setFocused(true);
    CHECK(v->isFocused() == true);

    // Idempotent: calling again with the same value must not crash, state stays.
    v->setFocused(true);
    CHECK(v->isFocused() == true);

    // setFocused(false) clears the state.
    v->setFocused(false);
    CHECK(v->isFocused() == false);
}

TEST_CASE("View focus state: setFocused on a CSD window is a safe no-op (no crash)") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
        server.dispatch();

    // CSD holdout: draw_frame == false, so setFocused must skip relayout.
    test::TestClient client(server.socketName(), 0xFF00FF00u, 200, 150,
                            test::TestClient::Deco::RequestCSD);
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
    // Let decoration handshake settle.
    for (int i = 0; i < 50; ++i) { client.flush(); server.dispatch(); client.pump(); }

    View *v = server.viewsForTest()[0].get();
    REQUIRE_FALSE(v->drawsFrame());   // confirm CSD path

    // Must not crash; state toggles but no relayout touches decoration buffers.
    CHECK(v->isFocused() == false);
    v->setFocused(true);
    CHECK(v->isFocused() == true);
    v->setFocused(false);
    CHECK(v->isFocused() == false);
}
