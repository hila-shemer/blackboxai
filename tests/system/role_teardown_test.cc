// Dogfooding crash 2026-07-10 (core: PID 24987, SIGSEGV at View.cc:27): a client
// destroyed its xdg_toplevel role but kept the wl_surface alive, then committed
// the bare surface again. The View outlived the role and its surface-commit
// listener dereferenced the freed wlr_xdg_toplevel. A toplevel that ceased to
// exist must take its View down with it, and a later role-less commit must land
// on no listener at all.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>

using namespace bbai;

TEST_CASE("destroying the toplevel role removes the View; a later bare-surface commit is harmless") {
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

    // Role teardown ahead of the wl_surface: the View must go with the role.
    client.destroyToplevelKeepSurface();
    auto gone = [&] { return server.viewsForTest().empty(); };
    for (int i = 0; i < 200 && !gone(); ++i) {
        client.flush();
        server.dispatch();
        client.pump();
    }
    CHECK(gone());

    // The live-session crash: committing the surviving role-less surface fired
    // the stale commit listener into freed toplevel memory.
    client.commitBareSurface();
    for (int i = 0; i < 40; ++i) {
        client.flush();
        server.dispatch();
        client.pump();
    }
    CHECK(server.viewsForTest().empty());

    // The compositor is still alive and compositing.
    test::Frame f = test::captureFrame(server);
    CHECK(f.w == 1280u);
    CHECK(f.h == 720u);
}
