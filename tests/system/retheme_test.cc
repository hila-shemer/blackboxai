// tests/system/retheme_test.cc
// Live re-theme: boot Results, applyStyleFile(Twice) repaints the world and
// persists the choice; reconfigure() rereads the rc; a missing style falls
// back (returns false). The rc lives at a writable /tmp copy because
// applyStyleFile writes session.styleFile back into it.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Style.hh"
#include "Output.hh"
#include "Toolbar.hh"
#include "Toolbar.geom.hh"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace bbai;

namespace {
  const char *kRc = "/tmp/bbai-retheme-test.rc";
  void writeRc(const std::string &body) {
    std::ofstream f(kRc, std::ios::trunc);
    f << body;
  }
  std::string slurp(const std::string &p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
  }
}

TEST_CASE("applyStyleFile: live repaint + persisted choice") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  writeRc("session.styleFile: data/styles/Results\n");

  Server server(/*headless=*/true, kRc);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();
  REQUIRE(server.currentStyle()->sourcePath() == "data/styles/Results");

  // Twice: rootCommand 'bsetroot -solid grey20' -> the desktop becomes a
  // grey20 solid after the swap.
  CHECK(server.applyStyleFile("data/styles/Twice"));
  CHECK(server.currentStyle()->sourcePath() == "data/styles/Twice");
  test::Frame f = test::captureFrame(server);
  auto rgb = [&](int x, int y) { return f.pixels[static_cast<size_t>(y) * f.w + x] & 0xFFFFFFu; };
  CHECK(rgb(5, 5) == 0x333333u);       // grey20 solid, not Results' modula
  CHECK(rgb(9, 3) == 0x333333u);

  // the choice persisted into the rc (classic saveStyleFilename)
  CHECK(slurp(kRc).find("session.styleFile: data/styles/Twice\n") != std::string::npos);

  // an unreadable style: false, nothing changes
  CHECK_FALSE(server.applyStyleFile("/nonexistent/style"));
  CHECK(server.currentStyle()->sourcePath() == "data/styles/Twice");
  std::remove(kRc);
}

TEST_CASE("re-theme with different toolbar metrics: the strut follows the drawn bar") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  writeRc("session.styleFile: data/styles/Results\n");

  Server server(/*headless=*/true, kRc);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  // A style whose computed bar height diverges from the constexpr defaults
  // via marginWidth (not font size - deterministic under the LiberationMono
  // pin): barHeight = labelHeight + 2*5, hiddenHeight = 5.
  const char *kStyle = "/tmp/bbai-retheme-margin.style";
  { std::ofstream f(kStyle, std::ios::trunc); f << "toolbar.marginWidth: 5\n"; }
  REQUIRE(server.applyStyleFile(kStyle));

  Toolbar *tb = server.toolbarForTest();
  REQUIRE(tb != nullptr);
  const toolbar::ToolbarMetrics m = server.currentStyle()->toolbarMetrics();
  const int barH = tb->barRectForTest().h;
  REQUIRE(barH == m.barHeight);
  REQUIRE(barH != toolbar::kBarHeight);   // else this test pins nothing

  // The strut must reserve what is actually drawn - a maximized window may
  // neither overlap the bar nor leave a dead gap.
  Output *out = server.activeOutputForTest();
  CHECK(out->workArea().height == out->fullBox().height - barH);

  // Auto-hide reserves the style's sliver, not the constexpr 2px.
  REQUIRE(m.hiddenHeight == 5);
  tb->setAutoHide(true);
  CHECK(out->workArea().height == out->fullBox().height - m.hiddenHeight);

  std::remove(kStyle);
  std::remove(kRc);
}

TEST_CASE("reconfigure: rereads the rc, re-applies knobs, reports style fallback") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  writeRc("session.styleFile: data/styles/Results\n");

  Server server(/*headless=*/true, kRc);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  FakeCommandRunner fake;
  server.setCommandRunnerForTest(&fake);

  // Edit the rc behind the server's back: new style, new knobs, a rootCommand.
  writeRc("session.styleFile: data/styles/Gray\n"
          "session.screen0.toolbar.placement: TopCenter\n"
          "session.screen0.workspaces: 6\n"
          "rootCommand: xsetroot -solid gray\n");
  CHECK(server.reconfigure());
  CHECK(server.currentStyle()->sourcePath() == "data/styles/Gray");
  // Gray's key is 8; the loader folds the handle border in (8 + 1*2), same
  // as Task 8's unit pin (style_test.cc "Gray" case).
  CHECK(server.currentStyle()->frameMetrics().handleHeight == 10);
  REQUIRE(server.toolbarForTest() != nullptr);
  CHECK(server.toolbarForTest()->placementForTest() == toolbar::Placement::TopCenter);
  CHECK(server.workspaces().count() == 6u);
  // rc rootCommand re-ran via /bin/sh; Gray's own bsetroot line did NOT spawn.
  REQUIRE(fake.runCount() == 1);
  CHECK(fake.lastCommand()[0] == "/bin/sh");
  CHECK(fake.lastCommand()[2] == "xsetroot -solid gray");

  writeRc("session.styleFile: /nonexistent/style\n");
  CHECK_FALSE(server.reconfigure());
  // builtin rung on ANY box: headless hides the install-prefix default
  // (Server::loadStyleWithFallback), so an installed product can't flip this.
  CHECK(server.currentStyle()->sourcePath().empty());

  // reconfigure with an override path switches the remembered rc.
  writeRc("session.styleFile: data/styles/Twice\n");
  CHECK(server.reconfigure(kRc));
  CHECK(server.currentStyle()->sourcePath() == "data/styles/Twice");
  std::remove(kRc);
}

TEST_CASE("style ladder middle rung: a pinned default style catches the fallback") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  writeRc("session.styleFile: /nonexistent/style\n");

  Server server(/*headless=*/true, kRc);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();
  // Headless boots with an EMPTY default rung: builtin, on any box.
  CHECK(server.currentStyle()->sourcePath().empty());

  // Pin a fake "installed default": the middle rung catches the fallback.
  server.setDefaultStyleForTest("data/styles/Results");
  CHECK_FALSE(server.reconfigure());   // the exact style still failed
  CHECK(server.currentStyle()->sourcePath() == "data/styles/Results");

  // Clear it: back to the builtin rung.
  server.setDefaultStyleForTest("");
  CHECK_FALSE(server.reconfigure());
  CHECK(server.currentStyle()->sourcePath().empty());
  std::remove(kRc);
}
