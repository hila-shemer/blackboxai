// tests/system/workspace_remove_test.cc
// Remove Last Workspace with tenants: views re-home to the last survivor,
// the current workspace follows when it is the one dying, and focus repair
// keeps a live handle (gotcha #29). Driven through the real menu (keyboard
// nav - no coordinates), because Act::RemoveWorkspace IS the only shrink
// path: the rc/applyConfig path stays grow-only (locked).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"
#include "Menu.hh"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <linux/input-event-codes.h>  // BTN_RIGHT

using namespace bbai;

namespace {
  const char *kRc = "/tmp/bbai-wsremove-test.rc";

  void boot(Server &server) {
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
      server.dispatch();
  }
  void settle(Server &server, test::TestClient &c) {
    auto mapped = [&] {
      const auto &v = server.viewsForTest();
      return !v.empty() && v.back()->isMapped();
    };
    for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); server.dispatch(); c.pump(); }
    REQUIRE(mapped());
    for (int i = 0; i < 30; ++i) { c.flush(); server.dispatch(); c.pump(); }
  }

  // Open the in-code root menu, keyboard-walk to Workspaces (row 3), Return
  // to cascade, walk to the last child row (Remove Last Workspace), Return.
  void removeLastViaMenu(Server &server) {
    server.injectPointerMotionForTest(700, 300);
    server.injectPointerButtonForTest(BTN_RIGHT, true);
    REQUIRE(server.menuOpenForTest());
    Menu *root = server.rootMenuForTest();
    // in-code menu rows: 0 kitty, 1 xterm, 2 sep, 3 Workspaces, 4 sep, 5 Restart, 6 Exit
    for (int i = 0; i < 10 && root->activeIndex() != 3; ++i)
      server.injectKeyForTest(XKB_KEY_Down, 0, true);
    REQUIRE(root->activeIndex() == 3);
    server.injectKeyForTest(XKB_KEY_Return, 0, true);
    Menu *ws = root->child();
    REQUIRE(ws != nullptr);
    const int last = ws->itemCount() - 1;             // Remove Last Workspace
    for (int i = 0; i < 16 && ws->activeIndex() != last; ++i)
      server.injectKeyForTest(XKB_KEY_Down, 0, true);
    REQUIRE(ws->activeIndex() == last);
    server.injectKeyForTest(XKB_KEY_Return, 0, true);
    REQUIRE_FALSE(server.menuOpenForTest());
  }
}

TEST_CASE("remove-last re-homes tenants, follows a dying current, repairs focus") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  { std::ofstream f(kRc, std::ios::trunc); f << "session.screen0.workspaces: 3\n"; }

  Server server(/*headless=*/true, kRc);
  boot(server);
  server.setMenuFileForTest("");   // pin the in-code menu (Workspaces submenu lives there)
  REQUIRE(server.workspaces().count() == 3u);

  // A on ws0 (current), B on ws2.
  test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  REQUIRE(a.ok());
  settle(server, a);
  View *va = server.viewsForTest()[0].get();

  server.setCurrentWorkspace(2);
  test::TestClient b(server.socketName(), 0xFF00FF00u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  REQUIRE(b.ok());
  settle(server, b);
  View *vb = server.viewsForTest()[1].get();
  REQUIRE(vb->workspace() == 2u);

  // Case 1: remove while current is elsewhere - B re-homes to ws1, hidden.
  server.setCurrentWorkspace(0);
  removeLastViaMenu(server);
  CHECK(server.workspaces().count() == 2u);
  CHECK(vb->workspace() == 1u);
  CHECK(server.currentWorkspaceForTest() == 0u);
  CHECK(server.focusedViewForTest() == va);         // ws0's focus untouched

  // The re-homed view is really there: switching to the survivor finds it.
  server.setCurrentWorkspace(1);
  CHECK(server.focusedViewForTest() == vb);

  // Case 2: remove while standing ON the dying workspace with focus there -
  // current follows to the survivor and the focused view keeps focus
  // (gotcha #29: aim the survivor's memory before the switch).
  removeLastViaMenu(server);
  CHECK(server.workspaces().count() == 1u);
  CHECK(server.currentWorkspaceForTest() == 0u);
  CHECK(vb->workspace() == 0u);
  CHECK(server.focusedViewForTest() == vb);

  // Floor: the last workspace never goes away.
  removeLastViaMenu(server);
  CHECK(server.workspaces().count() == 1u);
  CHECK(server.focusedViewForTest() == vb);
  std::remove(kRc);
}
