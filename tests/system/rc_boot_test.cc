// tests/system/rc_boot_test.cc
// Startup application of the per-screen workspace config: count + names land
// in the WorkspaceModel before the toolbar first renders.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Workspace.hh"

#include <cstdlib>

using namespace bbai;

TEST_CASE("workspace count and names apply from the rc at startup") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true, "tests/fixtures/rcboot.blackboxrc");
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  CHECK(server.workspaces().count() == 6u);
  CHECK(server.workspaces().name(0) == "web");
  CHECK(server.workspaces().name(2) == "chat");
  CHECK(server.workspaces().name(3) == "Workspace 4");   // unnamed -> default
  CHECK(server.currentWorkspaceForTest() == 0u);
  // The toolbar's workspace label shows the configured name from frame one.
  CHECK(server.toolbarForTest() != nullptr);
}

TEST_CASE("no rc: the 4 default workspaces") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  CHECK(server.workspaces().count() == 4u);
}

#include "TestClient.hh"
#include "View.hh"

namespace {
  // Map one client and return its View (nullptr on timeout).
  bbai::View *mapOne(bbai::Server &server, bbai::test::TestClient &client) {
    auto mapped = [&] {
      const auto &v = server.viewsForTest();
      return !v.empty() && v[0]->isMapped();
    };
    for (int i = 0; i < 500 && !mapped(); ++i) {
      client.flush(); server.dispatch(); client.pump();
    }
    return mapped() ? server.viewsForTest()[0].get() : nullptr;
  }
}

TEST_CASE("focusNewWindows True (default): a mapped window takes focus") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);   // no rc -> reference default True
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();
  test::TestClient client(server.socketName(), 0xFF00FF00u, 200, 150);
  REQUIRE(client.ok());
  View *v = mapOne(server, client);
  REQUIRE(v != nullptr);
  CHECK(server.focusedViewForTest() == v);
  CHECK(v->isFocused() == true);
}

TEST_CASE("focusNewWindows False: a mapped window stays unfocused") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true, "tests/fixtures/nofocusnew.blackboxrc");
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();
  test::TestClient client(server.socketName(), 0xFF00FF00u, 200, 150);
  REQUIRE(client.ok());
  View *v = mapOne(server, client);
  REQUIRE(v != nullptr);
  CHECK(server.focusedViewForTest() == nullptr);
  CHECK(v->isFocused() == false);
}
