// A user keys file (session.keyFile) drives the real onKey/executeAction path:
// a rebound chord fires its action, the displaced default goes quiet, the
// reserved escape valve survives, and reconfigure() re-reads the file live.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

using namespace bbai;

static const uint32_t SUPER = WLR_MODIFIER_LOGO;

static void writeFile(const std::string &path, const std::string &body) {
  std::ofstream(path) << body;
}

TEST_CASE("a keys file rebinds a chord; the displaced default no longer fires") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true, "tests/fixtures/keys-rebind.blackboxrc");
  REQUIRE(server.ok());
  CHECK(server.currentWorkspaceForTest() == 0);

  // The fixture rebinds Super+n -> NextWorkspace (not a built-in default).
  server.injectKeyForTest(XKB_KEY_n, SUPER, /*pressed=*/true);
  CHECK(server.lastActionForTest() == Action::WorkspaceNext);
  CHECK(server.currentWorkspaceForTest() == 1);

  // The built-in Super+Right is no longer bound (authoritative file).
  server.injectKeyForTest(XKB_KEY_Right, SUPER, true);
  CHECK(server.currentWorkspaceForTest() == 1);
}

TEST_CASE("the reserved escape valve survives an authoritative keys file") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true, "tests/fixtures/keys-rebind.blackboxrc");
  REQUIRE(server.ok());

  server.injectKeyForTest(XKB_KEY_BackSpace, WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, true);
  CHECK(server.lastActionForTest() == Action::Quit);
  server.dispatch();   // terminate() drains cleanly
}

TEST_CASE("reconfigure() re-reads an edited keys file live") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  const std::string keys = std::string(std::tmpnam(nullptr)) + ".keys";
  const std::string rc   = std::string(std::tmpnam(nullptr)) + ".blackboxrc";
  writeFile(keys, "Super n :NextWorkspace\n");
  writeFile(rc, "session.keyFile: " + keys + "\n");

  Server server(/*headless=*/true, rc);
  REQUIRE(server.ok());
  server.injectKeyForTest(XKB_KEY_n, SUPER, true);
  CHECK(server.currentWorkspaceForTest() == 1);

  // Rewrite the keys file to bind a different chord, then reconfigure.
  writeFile(keys, "Super m :PrevWorkspace\n");
  server.reconfigure("");
  server.injectKeyForTest(XKB_KEY_m, SUPER, true);          // now bound -> prev (wraps 1 -> 0)
  CHECK(server.lastActionForTest() == Action::WorkspacePrev);
  CHECK(server.currentWorkspaceForTest() == 0);
  server.injectKeyForTest(XKB_KEY_n, SUPER, true);          // old binding gone
  CHECK(server.currentWorkspaceForTest() == 0);

  std::remove(keys.c_str());
  std::remove(rc.c_str());
}
