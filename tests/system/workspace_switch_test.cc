// B5: switching workspaces shows/hides the right views, restores per-workspace
// focus, and updates the toolbar label. Client A (red) on ws0, client B (green)
// on ws1.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>  // BTN_LEFT

using namespace bbai;

namespace {
  void pumpUntilMapped(Server &s, test::TestClient &c, size_t want) {
    for (int i = 0; i < 800 && s.viewsForTest().size() < want; ++i) {
      c.flush(); s.dispatch(); c.pump();
    }
    for (int i = 0; i < 40; ++i) { c.flush(); s.dispatch(); c.pump(); }
  }
  void clickTitlebar(Server &s) {  // focus the window under (260,130)
    s.injectPointerMotionForTest(260, 130);
    s.injectPointerButtonForTest(BTN_LEFT, true);
    s.injectPointerButtonForTest(BTN_LEFT, false);
  }
}

TEST_CASE("workspace switch hides/shows views, restores focus, updates the label") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  // A (red) maps on ws0; focus it.
  test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150);
  REQUIRE(a.ok());
  pumpUntilMapped(server, a, 1);
  View *va = server.viewsForTest()[0].get();
  clickTitlebar(server);
  CHECK(server.focusedViewForTest() == va);

  // Switch to ws1: A hidden, no focus (empty workspace), label "Workspace 2".
  server.setCurrentWorkspace(1);
  CHECK(server.currentWorkspaceForTest() == 1);
  CHECK_FALSE(va->visible());
  CHECK(server.focusedViewForTest() == nullptr);

  // B (green) maps on ws1; focus it.
  test::TestClient b(server.socketName(), 0xFF00FF00u, 200, 150);
  REQUIRE(b.ok());
  for (int i = 0; i < 800 && server.viewsForTest().size() < 2; ++i) {
    a.flush(); b.flush(); server.dispatch(); a.pump(); b.pump();
  }
  for (int i = 0; i < 40; ++i) { a.flush(); b.flush(); server.dispatch(); a.pump(); b.pump(); }
  REQUIRE(server.viewsForTest().size() == 2);
  View *vb = server.viewsForTest()[1].get();
  CHECK(vb->workspace() == 1);
  CHECK(vb->visible());
  clickTitlebar(server);
  CHECK(server.focusedViewForTest() == vb);

  auto centre = [&] { return test::captureFrame(server).pixels[218u * 1280 + 261] & 0x00FFFFFFu; };
  CHECK(centre() == 0x0000FF00u);                 // only B (green) on ws1
  CHECK(test::compareGolden(test::captureFrame(server), "tests/golden/m4-ws2.png", 2, 40));

  // Switch back to ws0: A shown + focus restored (memory), B hidden, "Workspace 1".
  server.setCurrentWorkspace(0);
  CHECK(va->visible());
  CHECK_FALSE(vb->visible());
  CHECK(server.focusedViewForTest() == va);       // per-workspace focus memory
  CHECK(centre() == 0x00FF0000u);                 // only A (red)
  CHECK(test::compareGolden(test::captureFrame(server), "tests/golden/m4-ws1.png", 2, 40));
}
