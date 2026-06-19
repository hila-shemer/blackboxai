// F4.7: Icons menu — lists iconified windows; clicking an entry de-iconifies
// the window (show, raise, focus) and dismisses the menu. Opens via
// MIDDLE-click on bare desktop or Mod4+Alt+T keybinding.
// Nonvisual — no golden compare.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"
#include "Menu.hh"
#include "Menu.geom.hh"
#include "Frame.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>  // BTN_LEFT / BTN_MIDDLE

using namespace bbai;

namespace {
  // Menu item centre Y: same formula as menu_action_test.cc
  int itemY(int open_y, int title_h, int index) {
    return open_y + title_h + menu::kFrameMargin
         + index * menu::itemHeight(18, false)
         + menu::itemHeight(18, false) / 2;
  }

  // Pump until two clients are both mapped.
  void pumpUntilBothMapped(Server &server,
                           test::TestClient &ca, test::TestClient &cb,
                           int iterations = 500) {
    for (int i = 0; i < iterations; ++i) {
      ca.flush(); cb.flush();
      server.dispatch();
      ca.pump(); cb.pump();
      const auto &v = server.viewsForTest();
      if (v.size() >= 2 && v[0]->isMapped() && v[1]->isMapped()) break;
    }
    // Extra settle cycles so decorations commit.
    for (int i = 0; i < 30; ++i) {
      ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump();
    }
  }

  // Iconify view A by clicking its iconify button (F4.3 path).
  void clickIconifyButton(Server &server, View *A) {
    const int bx = A->x() + frame::iconifyButton(A->contentWidth(), A->contentHeight()).x
                          + frame::kButtonWidth / 2;
    const int by = A->y() + frame::iconifyButton(A->contentWidth(), A->contentHeight()).y
                          + frame::kButtonWidth / 2;
    server.injectPointerMotionForTest(bx, by);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
    server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
  }
} // anonymous namespace

TEST_CASE("Mod4+Alt+T opens the icon menu with correct item count") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  REQUIRE(ca.ok());
  test::TestClient cb(server.socketName(), 0xFF00FF00u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  REQUIRE(cb.ok());

  pumpUntilBothMapped(server, ca, cb);

  const auto &views = server.viewsForTest();
  REQUIRE(views.size() >= 2);
  View *A = views[0].get();
  View *B = views[1].get();

  // Move B so it doesn't overlap A.
  B->setPosition(160, 400);

  // Focus A then iconify it via the button.
  server.injectPointerMotionForTest(260, 130);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
  REQUIRE(server.focusedViewForTest() == A);

  clickIconifyButton(server, A);
  REQUIRE(A->isIconified());
  REQUIRE(server.focusedViewForTest() == B);

  // Open the icon menu via Mod4+Alt+T — must open and fire Action::IconMenu.
  server.injectKeyForTest(XKB_KEY_t, WLR_MODIFIER_LOGO | WLR_MODIFIER_ALT, /*pressed=*/true);
  CHECK(server.menuOpenForTest());
  CHECK(server.lastActionForTest() == Action::IconMenu);
  REQUIRE(server.rootMenuForTest() != nullptr);
  CHECK(server.rootMenuForTest()->itemCount() == 1);

  // Dismiss with Escape.
  server.injectKeyForTest(XKB_KEY_Escape, 0, /*pressed=*/true);
  CHECK_FALSE(server.menuOpenForTest());
}

TEST_CASE("middle-click on bare desktop opens the icon menu") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  REQUIRE(ca.ok());
  test::TestClient cb(server.socketName(), 0xFF00FF00u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  REQUIRE(cb.ok());

  pumpUntilBothMapped(server, ca, cb);

  const auto &views = server.viewsForTest();
  REQUIRE(views.size() >= 2);
  View *A = views[0].get();
  View *B = views[1].get();
  B->setPosition(160, 400);

  // Focus A then iconify it.
  server.injectPointerMotionForTest(260, 130);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
  REQUIRE(server.focusedViewForTest() == A);

  clickIconifyButton(server, A);
  REQUIRE(A->isIconified());

  // Middle-click on bare desktop opens the icon menu.
  server.injectPointerMotionForTest(800, 400);
  server.injectPointerButtonForTest(BTN_MIDDLE, /*pressed=*/true);
  CHECK(server.menuOpenForTest());
  REQUIRE(server.rootMenuForTest() != nullptr);
  CHECK(server.rootMenuForTest()->itemCount() == 1);

  // Dismiss.
  server.injectKeyForTest(XKB_KEY_Escape, 0, /*pressed=*/true);
  CHECK_FALSE(server.menuOpenForTest());
}

TEST_CASE("clicking an icon menu entry deiconifies the window") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  REQUIRE(ca.ok());
  test::TestClient cb(server.socketName(), 0xFF00FF00u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  REQUIRE(cb.ok());

  pumpUntilBothMapped(server, ca, cb);

  const auto &views = server.viewsForTest();
  REQUIRE(views.size() >= 2);
  View *A = views[0].get();
  View *B = views[1].get();
  B->setPosition(160, 400);

  // Focus A then iconify it.
  server.injectPointerMotionForTest(260, 130);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
  REQUIRE(server.focusedViewForTest() == A);

  clickIconifyButton(server, A);
  REQUIRE(A->isIconified());
  REQUIRE(server.focusedViewForTest() == B);

  // Open icon menu via middle-click.
  const int menu_x = 800, menu_y = 400;
  server.injectPointerMotionForTest(menu_x, menu_y);
  server.injectPointerButtonForTest(BTN_MIDDLE, /*pressed=*/true);
  REQUIRE(server.menuOpenForTest());
  REQUIRE(server.rootMenuForTest() != nullptr);
  REQUIRE(server.rootMenuForTest()->itemCount() == 1);

  // Click item 0 (A's entry).
  const int iy = itemY(menu_y, menu::titleHeight(18), 0);
  server.injectPointerMotionForTest(menu_x + 30, iy);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);

  // Menu must be dismissed.
  CHECK_FALSE(server.menuOpenForTest());
  // A must be deiconified.
  CHECK_FALSE(A->isIconified());
  CHECK(A->visible());
  // A must now have focus.
  CHECK(server.focusedViewForTest() == A);

  // Re-open the icon menu: it should now list 0 items.
  server.injectPointerMotionForTest(800, 400);
  server.injectPointerButtonForTest(BTN_MIDDLE, /*pressed=*/true);
  REQUIRE(server.menuOpenForTest());
  CHECK(server.rootMenuForTest()->itemCount() == 0);
  server.injectKeyForTest(XKB_KEY_Escape, 0, /*pressed=*/true);
}

TEST_CASE("deiconify from a different workspace brings window to current workspace") {
  // Regression: deiconifyView cleared iconified_ but left on_workspace_=false when
  // the window was iconified on ws0 and deiconified from the icon menu on ws1.
  // Result: focusView fired on an invisible window. Fix: reassign to current ws.
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  REQUIRE(ca.ok());
  test::TestClient cb(server.socketName(), 0xFF00FF00u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  REQUIRE(cb.ok());

  pumpUntilBothMapped(server, ca, cb);

  const auto &views = server.viewsForTest();
  REQUIRE(views.size() >= 2);
  View *A = views[0].get();
  View *B = views[1].get();
  B->setPosition(160, 400);

  // Step 1: confirm A is on workspace 0.
  REQUIRE(A->workspace() == 0u);
  REQUIRE(A->visible());

  // Step 2: iconify A from workspace 0.
  server.injectPointerMotionForTest(260, 130);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
  REQUIRE(server.focusedViewForTest() == A);

  clickIconifyButton(server, A);
  REQUIRE(A->isIconified());
  REQUIRE_FALSE(A->visible());

  // Step 3: switch to workspace 1. A.on_workspace_ becomes false (ws0 != ws1).
  server.setCurrentWorkspace(1);
  REQUIRE(server.currentWorkspaceForTest() == 1u);
  REQUIRE(A->workspace() == 0u);   // still logically on ws0

  // Step 4: open the icon menu from ws1 — A must appear (buildIconMenu lists all
  // workspaces, which is the precondition for the bug).
  const int menu_x = 800, menu_y = 400;
  server.injectPointerMotionForTest(menu_x, menu_y);
  server.injectPointerButtonForTest(BTN_MIDDLE, /*pressed=*/true);
  REQUIRE(server.menuOpenForTest());
  REQUIRE(server.rootMenuForTest() != nullptr);
  REQUIRE(server.rootMenuForTest()->itemCount() == 1);  // A is listed

  // Step 5: click A's entry → deiconifyView(A).
  const int iy = itemY(menu_y, menu::titleHeight(18), 0);
  server.injectPointerMotionForTest(menu_x + 30, iy);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);

  // Menu must be dismissed.
  CHECK_FALSE(server.menuOpenForTest());

  // THE BUG ASSERTIONS — these failed before the fix:
  // A must be visible (not just de-iconified; on_workspace_ must also be true).
  CHECK(A->visible());
  // Keyboard focus must not go to an invisible window.
  CHECK(server.focusedViewForTest() == A);
  // A must be on the current workspace (ws1), not still stranded on ws0.
  CHECK(A->workspace() == server.currentWorkspaceForTest());
  CHECK(A->workspace() == 1u);
}

TEST_CASE("destroyed iconified window is removed from icons list (no crash, 0 items)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  REQUIRE(ca.ok());
  test::TestClient cb(server.socketName(), 0xFF00FF00u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  REQUIRE(cb.ok());

  pumpUntilBothMapped(server, ca, cb);

  const auto &views = server.viewsForTest();
  REQUIRE(views.size() >= 2);
  View *A = views[0].get();
  View *B = views[1].get();
  B->setPosition(160, 400);

  // Focus A then iconify it.
  server.injectPointerMotionForTest(260, 130);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
  REQUIRE(server.focusedViewForTest() == A);

  clickIconifyButton(server, A);
  REQUIRE(A->isIconified());

  // Destroy A's client — removeView erases it from icons_.
  ca.closeWindow();
  for (int i = 0; i < 100 && server.viewsForTest().size() >= 2; ++i) {
    ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump();
  }
  // A is now dangling; server.viewsForTest() should have only B left.
  REQUIRE(server.viewsForTest().size() == 1);

  // Open the icon menu: must have 0 items and must not crash.
  server.injectPointerMotionForTest(800, 400);
  server.injectPointerButtonForTest(BTN_MIDDLE, /*pressed=*/true);
  CHECK(server.menuOpenForTest());
  CHECK(server.rootMenuForTest()->itemCount() == 0);
  server.injectKeyForTest(XKB_KEY_Escape, 0, /*pressed=*/true);
  CHECK_FALSE(server.menuOpenForTest());
}
