// F4.2: View iconified state composes with workspace visibility.
// F4.3: Clicking the iconify title-bar button iconifies the window and re-homes
//       focus; a release that drifts off the button is a no-op.
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

// --- F4.3: iconify button click dispatches on release-inside ------------------

// Helper: pump until both views are mapped.
static void pumpUntilMapped(Server &server,
                            test::TestClient &ca, test::TestClient &cb,
                            int iterations = 500) {
    for (int i = 0; i < iterations; ++i) {
        ca.flush(); cb.flush();
        server.dispatch();
        ca.pump(); cb.pump();
        const auto &v = server.viewsForTest();
        if (v.size() >= 2 && v[0]->isMapped() && v[1]->isMapped()) break;
    }
    // Extra settle cycles so decorations commit.
    for (int i = 0; i < 30; ++i) { ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump(); }
}

TEST_CASE("iconify button press+release-inside iconifies window and re-homes focus") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
        server.dispatch();

    // Map two SSD clients.
    // Client A lands at default position (160,120); client B is at (160,400).
    test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                        test::TestClient::Deco::RequestSSD);
    REQUIRE(ca.ok());
    test::TestClient cb(server.socketName(), 0xFF00FF00u, 200, 150,
                        test::TestClient::Deco::RequestSSD);
    REQUIRE(cb.ok());

    pumpUntilMapped(server, ca, cb);

    const auto &views = server.viewsForTest();
    REQUIRE(views.size() >= 2);
    REQUIRE(views[0]->isMapped());
    REQUIRE(views[1]->isMapped());

    View *A = views[0].get();
    View *B = views[1].get();

    // Move B so it does not overlap A.
    B->setPosition(160, 400);

    // Focus A by clicking its titlebar (the middle of the title area, away from
    // buttons).  A is at (160,120); titlebar y ~[120,143), use y=130, x=260.
    server.injectPointerMotionForTest(260, 130);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
    REQUIRE(server.focusedViewForTest() == A);

    // Compute A's iconify-button global position.
    // iconifyButton() returns {kTitleMargin, kTitleMargin, kButtonWidth, kButtonWidth}
    // = {2, 2, 19, 19} relative to the view's frame origin (A->x(), A->y()).
    const int bx = A->x() + frame::iconifyButton(A->contentWidth(), A->contentHeight()).x
                          + frame::kButtonWidth / 2;   // centre x
    const int by = A->y() + frame::iconifyButton(A->contentWidth(), A->contentHeight()).y
                          + frame::kButtonWidth / 2;   // centre y

    // Move cursor onto the button and click (press then release with cursor still inside).
    server.injectPointerMotionForTest(bx, by);
    REQUIRE(server.partAtForTest(bx, by) == Part::IconifyButton);

    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);

    // A must be iconified and invisible.
    CHECK(A->isIconified());
    CHECK_FALSE(A->visible());

    // Focus must have moved to B, not back to the now-iconified A.
    CHECK(server.focusedViewForTest() == B);
}

TEST_CASE("iconify button: release outside the button is a no-op") {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
        server.dispatch();

    // A single SSD client suffices: press iconify, drift off, release.
    test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                        test::TestClient::Deco::RequestSSD);
    REQUIRE(ca.ok());

    auto mapped = [&] {
        const auto &v = server.viewsForTest();
        return !v.empty() && v[0]->isMapped();
    };
    for (int i = 0; i < 500 && !mapped(); ++i) { ca.flush(); server.dispatch(); ca.pump(); }
    REQUIRE(mapped());
    for (int i = 0; i < 30; ++i) { ca.flush(); server.dispatch(); ca.pump(); }

    View *A = server.viewsForTest()[0].get();

    // Move onto the iconify button and press.
    const int bx = A->x() + frame::iconifyButton(A->contentWidth(), A->contentHeight()).x
                          + frame::kButtonWidth / 2;
    const int by = A->y() + frame::iconifyButton(A->contentWidth(), A->contentHeight()).y
                          + frame::kButtonWidth / 2;

    server.injectPointerMotionForTest(bx, by);
    REQUIRE(server.partAtForTest(bx, by) == Part::IconifyButton);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);

    // Drift cursor away to the desktop (far from the button).
    server.injectPointerMotionForTest(800, 600);

    // Release off the button — must NOT iconify.
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);

    CHECK_FALSE(A->isIconified());
    CHECK(A->visible());
}
