// Classic wheel gestures: scroll over the bare desktop cycles workspaces
// (session.changeWorkspaceWithMouseWheel), scroll over the toolbar footprint
// cycles too (session.toolbarActionsWithMouseWheel) - both default True. Up =
// next (classic button4, Screen.cc:2058-2063): axis delta < 0 -> +1.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Toolbar.hh"

#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
    REQUIRE(s.titleFont()->height() == 18);          // gotcha #20: bar geometry pinned
  }
  std::string writeRc(const std::string &body) {
    char t[] = "/tmp/bbai-wheel-XXXXXX";
    REQUIRE(mkdtemp(t) != nullptr);
    std::string p = std::string(t) + "/rc";
    std::ofstream(p) << body;
    return p;
  }
  void scrollUp(Server &s)   { s.injectPointerAxisForTest(WL_POINTER_AXIS_VERTICAL_SCROLL, -15.0, -120); }
  void scrollDown(Server &s) { s.injectPointerAxisForTest(WL_POINTER_AXIS_VERTICAL_SCROLL,  15.0,  120); }
}

TEST_CASE("desktop scroll cycles workspaces by default; up=next, down=prev") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  REQUIRE(server.currentWorkspaceForTest() == 0u);

  server.injectPointerMotionForTest(5, 5);          // bare desktop, top-left
  scrollUp(server);
  CHECK(server.currentWorkspaceForTest() == 1u);    // next
  scrollDown(server);
  CHECK(server.currentWorkspaceForTest() == 0u);    // prev (wraps with 4 ws)
}

TEST_CASE("toolbar scroll cycles workspaces (toolbarActions default True)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  Toolbar *tb = server.toolbarForTest();
  REQUIRE(tb != nullptr);
  const toolbar::Rect b = tb->barRectForTest();     // shown footprint, derived not baked
  server.injectPointerMotionForTest(b.x + b.w / 2, b.y + b.h / 2);
  scrollUp(server);
  CHECK(server.currentWorkspaceForTest() == 1u);
}

TEST_CASE("auto-hidden toolbar does not swallow the wheel over its shown footprint") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  // Desktop wheel-gate off, toolbar wheel-gate on: the only thing that could
  // cycle here is the toolbar footprint gate. With auto-hide the bar is off
  // screen, so the scroll belongs to whatever occupies the strip, not the bar.
  const std::string rc = writeRc("session.changeWorkspaceWithMouseWheel: False\n"
                                 "session.toolbarActionsWithMouseWheel: True\n");
  Server server(/*headless=*/true, rc);
  boot(server);
  Toolbar *tb = server.toolbarForTest();
  REQUIRE(tb != nullptr);
  tb->setAutoHide(true);
  REQUIRE(tb->hiddenForTest());
  const toolbar::Rect b = tb->barRectForTest();     // shown footprint
  server.injectPointerMotionForTest(b.x + b.w / 2, b.y + b.h / 2);
  scrollUp(server);
  CHECK(server.currentWorkspaceForTest() == 0u);    // hidden bar didn't eat the wheel
}

TEST_CASE("both gates respect False - scroll is inert on the desktop and bar") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  const std::string rc = writeRc("session.changeWorkspaceWithMouseWheel: False\n"
                                 "session.toolbarActionsWithMouseWheel: False\n");
  Server server(/*headless=*/true, rc);
  boot(server);
  server.injectPointerMotionForTest(5, 5);
  scrollUp(server);
  CHECK(server.currentWorkspaceForTest() == 0u);    // desktop gate off
  const toolbar::Rect b = server.toolbarForTest()->barRectForTest();
  server.injectPointerMotionForTest(b.x + b.w / 2, b.y + b.h / 2);
  scrollUp(server);
  CHECK(server.currentWorkspaceForTest() == 0u);    // toolbar gate off
}
