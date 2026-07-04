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
