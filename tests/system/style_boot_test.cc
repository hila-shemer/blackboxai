// tests/system/style_boot_test.cc
// The -rc load path end-to-end: Config -> style ladder -> Results' bsetroot
// modula background on screen. Also pins the rootCommand shell policy: the
// RC file's command goes through /bin/sh, the STYLE file's never runs.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Style.hh"

#include <cstdlib>

using namespace bbai;

TEST_CASE("booting with -rc loads the named style; Results' modula paints the desktop") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  // No rc rootCommand in this fixture: the style's bsetroot line is the
  // effective root command (classic fallback), so the modula paints.
  Server server(/*headless=*/true, "tests/fixtures/results-nocmd.blackboxrc");
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  REQUIRE(server.currentStyle() != nullptr);
  CHECK(server.currentStyle()->sourcePath() == "data/styles/Results");
  CHECK(server.config().styleFile == "data/styles/Results");

  test::Frame f = test::captureFrame(server);
  REQUIRE(f.w == 1280u);
  auto rgb = [&](int x, int y) { return f.pixels[static_cast<size_t>(y) * f.w + x] & 0xFFFFFFu; };
  // Results: bsetroot -mod 4 4 -fg rgb:6/6/5c -bg grey20.
  // Row 0 is all-fg; col rule fg at x%16 in {3,7,11,15}.
  CHECK(rgb(0, 0) == 0x66665cu);   // fg row
  CHECK(rgb(3, 1) == 0x66665cu);   // fg column
  CHECK(rgb(1, 1) == 0x333333u);   // grey20 background
  CHECK(rgb(1, 2) == 0x333333u);

  CHECK(test::compareGolden(f, "tests/golden/v1-results-desktop.png", 2, 0));
}

TEST_CASE("rootCommand policy: rc command runs via /bin/sh, style command never runs") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true, "tests/fixtures/results.blackboxrc");
  REQUIRE(server.ok());
  FakeCommandRunner fake;
  server.setCommandRunnerForTest(&fake);

  server.runRootCommandForTest();
  REQUIRE(fake.runCount() == 1);
  const std::vector<std::string> &argv = fake.lastCommand();
  REQUIRE(argv.size() == 3);
  CHECK(argv[0] == "/bin/sh");
  CHECK(argv[1] == "-c");
  CHECK(argv[2] == "xsetroot -solid gray");   // the RC line, verbatim
  // Results' own rootCommand (bsetroot ...) was interpreted, not spawned:
  // exactly one run happened.

  // Classic precedence end-to-end: the rc rootCommand suppresses the style's
  // bsetroot entirely, and since xsetroot can't paint us (non-bsetroot, no
  // layer-shell), the desktop is flat black - NOT Results' modula.
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();
  test::Frame f = test::captureFrame(server);
  auto rgb = [&](int x, int y) { return f.pixels[static_cast<size_t>(y) * f.w + x] & 0xFFFFFFu; };
  CHECK(rgb(0, 0) == 0x000000u);
  CHECK(rgb(3, 1) == 0x000000u);   // would be fg 0x66665c if the modula won
}

TEST_CASE("headless with no rc: builtin style, no HOME leakage") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  CHECK(server.currentStyle()->sourcePath().empty());   // builtin rung
  FakeCommandRunner fake;
  server.setCommandRunnerForTest(&fake);
  server.runRootCommandForTest();
  CHECK(fake.runCount() == 0);   // no rc -> no rootCommand
}
