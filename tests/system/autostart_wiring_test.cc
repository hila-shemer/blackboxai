// M7: the Server-side autostart wiring - glob the autostart dirs, parse +
// filter each .desktop (Task 1's pure layer), skip Hidden / wrong-desktop /
// missing-TryExec, dedup user-over-system by basename, and spawn the survivors
// through the (injectable) CommandRunner. A FakeCommandRunner records the argv
// so nothing real is launched; temp dirs hold the fixtures.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "Server.hh"
#include "CommandRunner.hh"

#include <cstdlib>
#include <fstream>
#include <string>

using namespace bbai;

namespace {
  std::string makeTempDir() {
    char tmpl[] = "/tmp/bbai-autostart-XXXXXX";
    char *p = mkdtemp(tmpl);
    REQUIRE(p != nullptr);
    return std::string(p);
  }
  void writeDesktop(const std::string &dir, const std::string &name,
                    const std::string &contents) {
    std::ofstream f(dir + "/" + name);
    f << contents;
  }
  Server bootHeadless() {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);
    return Server(/*headless=*/true);
  }
}

TEST_CASE("autostart launches visible entries and skips Hidden") {
  Server server = bootHeadless();
  REQUIRE(server.ok());
  std::string dir = makeTempDir();
  writeDesktop(dir, "good.desktop",
               "[Desktop Entry]\nType=Application\nExec=good-app --flag\n");
  writeDesktop(dir, "hidden.desktop",
               "[Desktop Entry]\nType=Application\nExec=bad-app\nHidden=true\n");

  FakeCommandRunner fake;
  server.setCommandRunnerForTest(&fake);
  server.setAutostartDirsForTest({dir});
  server.runAutostartForTest();

  CHECK(fake.runCount() == 1);
  CHECK(fake.lastCommand().back().find("good-app") != std::string::npos);
}

TEST_CASE("autostart skips an entry whose TryExec is not on PATH") {
  Server server = bootHeadless();
  REQUIRE(server.ok());
  std::string dir = makeTempDir();
  writeDesktop(dir, "ghost.desktop",
               "[Desktop Entry]\nExec=ghost\nTryExec=/nonexistent/ghost-bin\n");

  FakeCommandRunner fake;
  server.setCommandRunnerForTest(&fake);
  server.setAutostartDirsForTest({dir});
  server.runAutostartForTest();

  CHECK(fake.runCount() == 0);
}

TEST_CASE("autostart runs a PATH-resolved TryExec entry and strips field codes") {
  Server server = bootHeadless();
  REQUIRE(server.ok());
  std::string dir = makeTempDir();
  // TryExec=sh is a bare name resolved against PATH; the %U/%i codes are dropped.
  writeDesktop(dir, "player.desktop",
               "[Desktop Entry]\nExec=player %U --x %i\nTryExec=sh\n");

  FakeCommandRunner fake;
  server.setCommandRunnerForTest(&fake);
  server.setAutostartDirsForTest({dir});
  server.runAutostartForTest();

  REQUIRE(fake.runCount() == 1);
  CHECK(fake.lastCommand() ==
        std::vector<std::string>{"/bin/sh", "-c", "player --x"});
}

TEST_CASE("a user-dir entry shadows the system-dir entry of the same basename") {
  Server server = bootHeadless();
  REQUIRE(server.ok());
  std::string user = makeTempDir(), sys = makeTempDir();
  writeDesktop(user, "same.desktop", "[Desktop Entry]\nExec=from-user\n");
  writeDesktop(sys,  "same.desktop", "[Desktop Entry]\nExec=from-system\n");

  FakeCommandRunner fake;
  server.setCommandRunnerForTest(&fake);
  server.setAutostartDirsForTest({user, sys});   // user dir first -> wins
  server.runAutostartForTest();

  CHECK(fake.runCount() == 1);
  CHECK(fake.lastCommand().back().find("from-user") != std::string::npos);
}
