// ext-workspace-v1 export: a real wayland-client binds ext_workspace_manager_v1,
// drains the workspace/group/name/state burst, and asserts our WorkspaceModel is
// mirrored out (count, names, exactly-one-active). Later cases drive an ACTIVATE
// request back in and mutate the model's size.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "ExtWorkspaceTestClient.hh"
#include "Server.hh"
#include "Workspace.hh"

#include <cstdlib>

using namespace bbai;

namespace {
  void bootOutput(Server &server) {
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
      server.dispatch();
    REQUIRE(server.activeSceneOutputForTest() != nullptr);
  }
  template <typename Cond, typename Pump>
  bool pumpUntil(Server &server, Cond cond, Pump pump, int iters = 1000) {
    for (int i = 0; i < iters && !cond(); ++i) { pump(); server.dispatch(); pump(); }
    return cond();
  }
}

TEST_CASE("ext-workspace mirrors the model out: 4 workspaces, names, one active") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootOutput(server);

  test::ExtWorkspaceTestClient c(server.socketName());
  REQUIRE(c.ok());

  bool ready = pumpUntil(server,
      [&] { return c.sawManager() && c.workspaceCount() == 4; },
      [&] { c.flush(); c.pump(); });
  CHECK(ready);
  CHECK(c.workspaceCount() == 4);
  CHECK(c.name(0) == "Workspace 1");   // WorkspaceModel default: "Workspace N", N=i+1
  CHECK(c.name(3) == "Workspace 4");
  CHECK(c.activeIndex() == static_cast<int>(server.workspaces().current()));
}

TEST_CASE("client ACTIVATE routes through setCurrentWorkspace") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootOutput(server);

  test::ExtWorkspaceTestClient c(server.socketName());
  REQUIRE(c.ok());
  REQUIRE(pumpUntil(server, [&] { return c.workspaceCount() == 4; },
                    [&] { c.flush(); c.pump(); }));
  REQUIRE(server.workspaces().current() == 0);   // boots on workspace 0

  c.activate(2);
  bool moved = pumpUntil(server,
      [&] { return server.workspaces().current() == 2; },
      [&] { c.flush(); c.pump(); });
  CHECK(moved);
  CHECK(server.workspaces().current() == 2);
  // The active bit is mirrored back out to the client.
  bool mirrored = pumpUntil(server, [&] { return c.activeIndex() == 2; },
                            [&] { c.flush(); c.pump(); });
  CHECK(mirrored);
}

TEST_CASE("client ACTIVATE is refused while the session is locked") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootOutput(server);

  test::ExtWorkspaceTestClient c(server.socketName());
  REQUIRE(c.ok());
  REQUIRE(pumpUntil(server, [&] { return c.workspaceCount() == 4; },
                    [&] { c.flush(); c.pump(); }));
  REQUIRE(server.workspaces().current() == 0);

  // Locking must gate the ext-workspace switch exactly as it gates Super+arrow:
  // routing to an empty workspace would clearFocus() the lock surface and deny
  // password entry. A hostile pager cannot become a lock-screen bypass.
  server.lockForTest();
  c.activate(2);
  bool moved = pumpUntil(server,
      [&] { return server.workspaces().current() == 2; },
      [&] { c.flush(); c.pump(); }, /*iters=*/200);
  CHECK_FALSE(moved);
  CHECK(server.workspaces().current() == 0);   // still on the pre-lock workspace
}

TEST_CASE("client ACTIVATE is refused while a modal menu owns input") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootOutput(server);

  test::ExtWorkspaceTestClient c(server.socketName());
  REQUIRE(c.ok());
  REQUIRE(pumpUntil(server, [&] { return c.workspaceCount() == 4; },
                    [&] { c.flush(); c.pump(); }));
  REQUIRE(server.workspaces().current() == 0);

  // A modal mode owns input; the key path swallows WorkspaceNext here, so the
  // pager mirror must refuse too - no switching the ground out from under an
  // open menu.
  server.openRootMenu(400, 300);
  REQUIRE(server.menuOpenForTest());
  c.activate(1);
  bool moved = pumpUntil(server,
      [&] { return server.workspaces().current() == 1; },
      [&] { c.flush(); c.pump(); }, /*iters=*/200);
  CHECK_FALSE(moved);
  CHECK(server.workspaces().current() == 0);
}

TEST_CASE("adding and removing a workspace reconciles the handle vector") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootOutput(server);

  test::ExtWorkspaceTestClient c(server.socketName());
  REQUIRE(c.ok());
  REQUIRE(pumpUntil(server, [&] { return c.workspaceCount() == 4; },
                    [&] { c.flush(); c.pump(); }));

  // Grow: a 5th workspace appears out to the client.
  server.workspaces().addWorkspace();
  server.syncExtWorkspacesForTest();
  CHECK(server.extWorkspaceHandleCountForTest() == 5);
  bool grew = pumpUntil(server, [&] { return c.workspaceCount() == 5; },
                        [&] { c.flush(); c.pump(); });
  CHECK(grew);
  CHECK(c.name(4) == "Workspace 5");

  // Shrink: removeLastWorkspaceAndRehome pops the tail; the client sees `removed`.
  server.removeLastWorkspaceAndRehome();
  CHECK(server.extWorkspaceHandleCountForTest() == 4);
  bool shrank = pumpUntil(server, [&] { return c.workspaceCount() == 4; },
                          [&] { c.flush(); c.pump(); });
  CHECK(shrank);
  // No abort reaching here == destroy path is group-detach-clean.
}
