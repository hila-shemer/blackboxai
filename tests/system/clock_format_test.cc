// tests/system/clock_format_test.cc
// session.screen0.strftimeFormat reaches the toolbar clock. The compiled
// default format equals bt::formatClock's default, so every existing clock
// golden holds byte-identically - pinned here as its own case. Custom cases
// use %H/%M only (locale-immune; %p would bend under a non-C LC_TIME).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Toolbar.hh"

#include <cstdio>
#include <cstdlib>
#include <fstream>

using namespace bbai;

namespace {
  const char *kRc = "/tmp/bbai-clockformat-test.rc";
  void writeRc(const std::string &body) {
    std::ofstream f(kRc, std::ios::trunc);
    f << body;
  }
  void boot(Server &server) {
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
      server.dispatch();
    REQUIRE(server.toolbarForTest() != nullptr);
  }
}

TEST_CASE("a custom strftimeFormat renders in the toolbar clock") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  writeRc("session.screen0.strftimeFormat: %H:%M\n");

  Server server(/*headless=*/true, kRc);
  boot(server);
  CHECK(server.toolbarForTest()->clockTextForTest() == "14:05");   // VirtualClock epoch, 24h
  server.advanceClockForTest(60);
  CHECK(server.toolbarForTest()->clockTextForTest() == "14:06");   // the tick re-renders through it
  std::remove(kRc);
}

TEST_CASE("no key: the classic default format is unchanged (existing goldens hold)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  CHECK(server.toolbarForTest()->clockTextForTest() == "02:05 PM");
}

TEST_CASE("reconfigure() re-reads the format and re-renders the clock") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  writeRc("session.styleFile: data/styles/Results\n");   // exact style so reconfigure() == true

  Server server(/*headless=*/true, kRc);
  boot(server);
  CHECK(server.toolbarForTest()->clockTextForTest() == "02:05 PM");

  writeRc("session.styleFile: data/styles/Results\n"
          "session.screen0.strftimeFormat: %H.%M\n");
  CHECK(server.reconfigure());   // restyle -> toolbar rebuild -> redrawClock
  CHECK(server.toolbarForTest()->clockTextForTest() == "14.05");
  std::remove(kRc);
}
