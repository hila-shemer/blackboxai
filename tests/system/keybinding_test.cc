// B3: keybindings drive workspace actions through the deviceless key injector
// (the same matcher real onKey uses). Side-effect = the current workspace; the
// toolbar label re-render is asserted visually in B5.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "Server.hh"

#include <cstdlib>

using namespace bbai;

static const uint32_t SUPER = WLR_MODIFIER_LOGO;

TEST_CASE("Mod4 keybindings switch the current workspace") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  CHECK(server.currentWorkspaceForTest() == 0);

  server.injectKeyForTest(XKB_KEY_Right, SUPER, /*pressed=*/true);
  CHECK(server.lastActionForTest() == Action::WorkspaceNext);
  CHECK(server.currentWorkspaceForTest() == 1);

  server.injectKeyForTest(XKB_KEY_3, SUPER, true);   // by-number -> index 2
  CHECK(server.currentWorkspaceForTest() == 2);

  server.injectKeyForTest(XKB_KEY_Left, SUPER, true);
  CHECK(server.currentWorkspaceForTest() == 1);

  // Next wraps 4 -> 0 (default 4 workspaces).
  server.injectKeyForTest(XKB_KEY_1, SUPER, true);   // -> 0
  server.injectKeyForTest(XKB_KEY_Left, SUPER, true);// prev wraps -> 3
  CHECK(server.currentWorkspaceForTest() == 3);
}

TEST_CASE("unbound keys and Mod4+q with no focus are harmless") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());

  server.injectKeyForTest(XKB_KEY_z, SUPER, true);   // unbound -> no workspace change
  CHECK(server.currentWorkspaceForTest() == 0);
  // Mod4+q with no focused window must not crash.
  server.injectKeyForTest(XKB_KEY_q, SUPER, true);
  CHECK(server.currentWorkspaceForTest() == 0);
}
