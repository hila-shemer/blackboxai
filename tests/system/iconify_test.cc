// F4.2: View iconified state composes with workspace visibility.
// The frame is shown only when on the current workspace AND not iconified.
// Nonvisual — no golden compare.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>

using namespace bbai;

TEST_CASE("View iconified state composes with workspace visibility") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
        server.dispatch();

    // Map one SSD client.
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

    // On current workspace, not iconified -> visible.
    CHECK(v->visible());

    // Iconify -> hidden.
    v->setIconified(true);
    CHECK(v->isIconified());
    CHECK_FALSE(v->visible());

    // Still on workspace, but iconified -> stays hidden.
    v->setOnWorkspace(true);
    CHECK_FALSE(v->visible());

    // Un-iconify on current workspace -> shown again.
    v->setIconified(false);
    CHECK(v->visible());
}

TEST_CASE("View setIconified is idempotent") {
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

    View *v = server.viewsForTest()[0].get();

    // Repeated setIconified(true) must not crash and state stays consistent.
    v->setIconified(true);
    v->setIconified(true);
    CHECK(v->isIconified());
    CHECK_FALSE(v->visible());

    v->setIconified(false);
    v->setIconified(false);
    CHECK_FALSE(v->isIconified());
    CHECK(v->visible());
}

TEST_CASE("View workspace-off + iconified: un-iconify keeps frame hidden") {
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

    View *v = server.viewsForTest()[0].get();

    // Hide via workspace switch then iconify.
    v->setOnWorkspace(false);
    CHECK_FALSE(v->visible());

    v->setIconified(true);
    CHECK_FALSE(v->visible());

    // Un-iconify while still off-workspace -> remains hidden.
    v->setIconified(false);
    CHECK_FALSE(v->visible());

    // Bring back to workspace -> now visible (not iconified).
    v->setOnWorkspace(true);
    CHECK(v->visible());
}
