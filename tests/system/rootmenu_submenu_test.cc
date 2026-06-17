// F3.3: hovering the "Workspaces" submenu row opens the child menu to the
// right; hovering a different parent row closes it.
// F3.4: keyboard + mouse navigation and leaf actions through the open submenu
// chain: Return/click on Workspaces opens the child; Down/Up navigate the
// child; Return/click on a leaf workspace/New/Remove performs the action and
// dismisses the whole chain.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Menu.hh"
#include "Menu.geom.hh"
#include "Workspace.hh"
#include "Rootmenu.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>  // BTN_RIGHT / BTN_LEFT

using namespace bbai;

namespace {
  // Global Y of the centre of item `index` in a menu opened at oy,
  // given the root-menu layout (items 0,1 are Commands, 2 is separator).
  int itemCentreY(int open_y, int index) {
    const int title_h = menu::titleHeight(18);
    int y = open_y + title_h + menu::kFrameMargin;
    for (int i = 0; i <= index; ++i) {
      const bool sep = (i == 2);            // root menu: index 2 is the separator
      const int h = sep ? menu::itemHeight(0, true) : menu::itemHeight(18, false);
      if (i == index) return y + h / 2;
      y += h;
    }
    return y;
  }
}

TEST_CASE("hovering the Workspaces row opens the child submenu to the right") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  const int ox = 400, oy = 200;
  server.injectPointerMotionForTest(ox, oy);
  server.injectPointerButtonForTest(BTN_RIGHT, /*pressed=*/true);
  REQUIRE(server.menuOpenForTest());

  // Workspaces is item index 3 (0=foot, 1=xterm, 2=sep, 3=Workspaces).
  const int ws_y = itemCentreY(oy, 3);
  server.injectPointerMotionForTest(ox + 30, ws_y);

  Menu *root = server.rootMenuForTest();
  REQUIRE(root != nullptr);

  CHECK(root->submenuOpenForTest());
  CHECK(root->openSubmenuIndexForTest() == 3);

  // Submenu item count: workspace count + sep + New + Remove = ws.count() + 3.
  WorkspaceModel ws;
  const int expected_count = static_cast<int>(ws.count()) + 3;
  CHECK(root->submenuItemCountForTest() == expected_count);

  // Child opens to the right of the parent (child X >= parent right edge).
  CHECK(root->childRectXForTest() >= root->rectXForTest() + root->itemCount());  // rough sanity
  // More precise: childRectX should be > rootRectX (at least the menu width apart)
  CHECK(root->childRectXForTest() > root->rectXForTest());

  // Golden: root menu + open child submenu.
  CHECK(test::compareGolden(test::captureFrame(server),
                            "tests/golden/m4-rootmenu-submenu.png", 2, 80));
}

TEST_CASE("hovering a non-submenu row closes the open child") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  const int ox = 400, oy = 200;
  server.injectPointerMotionForTest(ox, oy);
  server.injectPointerButtonForTest(BTN_RIGHT, /*pressed=*/true);
  REQUIRE(server.menuOpenForTest());

  // First open the submenu by hovering Workspaces (index 3).
  server.injectPointerMotionForTest(ox + 30, itemCentreY(oy, 3));
  REQUIRE(server.rootMenuForTest()->submenuOpenForTest());

  // Now hover over "foot" (index 0) — not a submenu, so child should close.
  server.injectPointerMotionForTest(ox + 30, itemCentreY(oy, 0));
  CHECK_FALSE(server.rootMenuForTest()->submenuOpenForTest());
  CHECK(server.menuOpenForTest());   // root menu itself stays open
}

// ---- F3.4 helpers ----------------------------------------------------------

namespace {
  // Centre Y of child-menu item at `ci`, given the child menu's origin.
  // Uses the same geometry math as the production layout.
  int childItemCentreY(const Menu *child, int ci) {
    const int title_h = menu::titleHeight(18);
    // Default submenu: all items are normal height except the separator at index ws_count.
    // We compute by walking — matches the production layout exactly.
    int y = child->rectYForTest() + title_h + menu::kFrameMargin;
    for (int i = 0; i <= ci; ++i) {
      // Separator is the item right after the workspace entries: for 4 workspaces, index 4.
      // We detect separators by checking if item i is a separator kind.
      // Build a default ws model to know the separator position.
      WorkspaceModel ws;
      const bool sep = (static_cast<unsigned>(i) == ws.count());
      const int h = menu::itemHeight(18, sep);
      if (i == ci) return y + h / 2;
      y += h;
    }
    return y;
  }
}

// F3.4-a: keyboard — Down to Workspaces, Return opens child; Down to Workspace 2,
// Return switches workspace and dismisses the whole chain.
TEST_CASE("F3.4 keyboard: Return on Workspaces opens child; leaf action switches workspace") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  const int ox = 400, oy = 200;
  server.injectPointerMotionForTest(ox, oy);
  server.injectPointerButtonForTest(BTN_RIGHT, /*pressed=*/true);
  REQUIRE(server.menuOpenForTest());

  // Navigate root: Down×3 → index 3 (Workspaces), skipping separator at 2.
  server.injectKeyForTest(XKB_KEY_Down, 0, true);  // → 0 (foot)
  server.injectKeyForTest(XKB_KEY_Down, 0, true);  // → 1 (xterm)
  server.injectKeyForTest(XKB_KEY_Down, 0, true);  // → 3 (Workspaces, skips sep 2)
  CHECK(server.activeMenuItemForTest() == 3);

  // Return on the Workspaces submenu row: child opens, menu stays open.
  server.injectKeyForTest(XKB_KEY_Return, 0, true);
  REQUIRE(server.menuOpenForTest());
  Menu *root = server.rootMenuForTest();
  REQUIRE(root != nullptr);
  CHECK(root->submenuOpenForTest());

  // Child is now live and its first selectable item (Workspace 1, index 0) is active.
  Menu *child = root->child();
  REQUIRE(child != nullptr);
  CHECK(child->activeIndex() == 0);

  // Down in the child → Workspace 2 (index 1).
  server.injectKeyForTest(XKB_KEY_Down, 0, true);
  CHECK(child->activeIndex() == 1);

  // Return on Workspace 2: switch to ws 1 (0-indexed) + dismiss whole chain.
  server.injectKeyForTest(XKB_KEY_Return, 0, true);
  CHECK_FALSE(server.menuOpenForTest());
  CHECK(server.currentWorkspaceForTest() == 1);
}

// F3.4-b: keyboard — navigate to New Workspace in the child; Return adds a workspace.
TEST_CASE("F3.4 keyboard: New Workspace via submenu adds a workspace") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  const unsigned count_before = server.workspaces().count();

  const int ox = 400, oy = 200;
  server.injectPointerMotionForTest(ox, oy);
  server.injectPointerButtonForTest(BTN_RIGHT, /*pressed=*/true);
  REQUIRE(server.menuOpenForTest());

  // Navigate to Workspaces (index 3) and open it.
  server.injectKeyForTest(XKB_KEY_Down, 0, true);  // → 0
  server.injectKeyForTest(XKB_KEY_Down, 0, true);  // → 1
  server.injectKeyForTest(XKB_KEY_Down, 0, true);  // → 3
  server.injectKeyForTest(XKB_KEY_Return, 0, true);  // open child
  REQUIRE(server.menuOpenForTest());
  Menu *child = server.rootMenuForTest()->child();
  REQUIRE(child != nullptr);

  // Navigate child past all workspaces and the separator to "New Workspace".
  // Default: 4 workspaces → sep at 4 → New Workspace at 5.
  // Down×4 from index 0: 0→1→2→3→skip4(sep)→5.
  for (int i = 0; i < 4; ++i)
    server.injectKeyForTest(XKB_KEY_Down, 0, true);
  CHECK(child->activeIndex() == 5);  // New Workspace

  server.injectKeyForTest(XKB_KEY_Return, 0, true);
  CHECK_FALSE(server.menuOpenForTest());
  CHECK(server.workspaces().count() == count_before + 1);
}

// F3.4-c: keyboard — Remove Last Workspace via submenu; floor at 1.
TEST_CASE("F3.4 keyboard: Remove Last Workspace via submenu removes; never below 1") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  const unsigned count_before = server.workspaces().count();
  REQUIRE(count_before >= 2);

  // Activate Remove once: should decrease by 1.
  auto activateRemove = [&]() {
    const int ox = 400, oy = 200;
    server.injectPointerMotionForTest(ox, oy);
    server.injectPointerButtonForTest(BTN_RIGHT, /*pressed=*/true);
    REQUIRE(server.menuOpenForTest());
    server.injectKeyForTest(XKB_KEY_Down, 0, true);  // → 0
    server.injectKeyForTest(XKB_KEY_Down, 0, true);  // → 1
    server.injectKeyForTest(XKB_KEY_Down, 0, true);  // → 3 (Workspaces)
    server.injectKeyForTest(XKB_KEY_Return, 0, true);  // open child
    REQUIRE(server.menuOpenForTest());
    Menu *child = server.rootMenuForTest()->child();
    REQUIRE(child != nullptr);
    // Navigate to Remove Last Workspace: index = ws.count() + 2
    // (ws items, 1 sep, New Workspace, Remove Last Workspace)
    // Down from index 0 enough times to reach it.
    for (int i = 0; i < 20 && child->activeIndex() < child->itemCount() - 1; ++i) {
      const int prev = child->activeIndex();
      server.injectKeyForTest(XKB_KEY_Down, 0, true);
      if (child->activeIndex() == prev) break;  // wrapped: stop
    }
    // The last selectable item in the child is Remove Last Workspace.
    CHECK(child->activeIndex() == child->itemCount() - 1);
    server.injectKeyForTest(XKB_KEY_Return, 0, true);
    CHECK_FALSE(server.menuOpenForTest());
  };

  activateRemove();
  CHECK(server.workspaces().count() == count_before - 1);

  // Remove until we'd go below 1: WorkspaceModel clamps at 1.
  // Remove (count-1) more times — each one should be a no-op at 1 ws.
  const unsigned remaining = server.workspaces().count();
  for (unsigned r = 0; r < remaining; ++r)
    activateRemove();
  CHECK(server.workspaces().count() >= 1);
}

// F3.4-d: mouse — hover opens child (F3.3), click child workspace row switches + dismisses.
TEST_CASE("F3.4 mouse: click child workspace row switches workspace and closes chain") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  const int ox = 400, oy = 200;
  server.injectPointerMotionForTest(ox, oy);
  server.injectPointerButtonForTest(BTN_RIGHT, /*pressed=*/true);
  REQUIRE(server.menuOpenForTest());

  // Hover the Workspaces row to open the child (F3.3 behaviour).
  server.injectPointerMotionForTest(ox + 30, itemCentreY(oy, 3));
  Menu *root = server.rootMenuForTest();
  REQUIRE(root != nullptr);
  REQUIRE(root->submenuOpenForTest());

  // Click child item 1 (Workspace 2, 0-indexed ws 1).
  Menu *child = root->child();
  REQUIRE(child != nullptr);
  const int cy = childItemCentreY(child, 1);
  const int cx = child->rectXForTest() + 30;
  server.injectPointerMotionForTest(cx, cy);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);

  CHECK_FALSE(server.menuOpenForTest());
  CHECK(server.currentWorkspaceForTest() == 1);
}

// F3.5-a: hovering a child row highlights it (chain-aware motion).
TEST_CASE("F3.5 child row highlights on hover") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  const int ox = 400, oy = 200;
  server.injectPointerMotionForTest(ox, oy);
  server.injectPointerButtonForTest(BTN_RIGHT, /*pressed=*/true);
  REQUIRE(server.menuOpenForTest());

  // Hover Workspaces (index 3) — opens the child.
  server.injectPointerMotionForTest(ox + 30, itemCentreY(oy, 3));
  Menu *root = server.rootMenuForTest();
  REQUIRE(root != nullptr);
  REQUIRE(root->submenuOpenForTest());

  // Now hover child item 1 (Workspace 2).
  Menu *child = root->child();
  REQUIRE(child != nullptr);
  const int cy = childItemCentreY(child, 1);
  const int cx = child->rectXForTest() + child->itemCount() / 2 + 10;  // inside child width
  server.injectPointerMotionForTest(cx, cy);

  CHECK(child->activeIndex() == 1);
}

// F3.5-b: hovering a plain parent row while child is open closes the child.
TEST_CASE("F3.5 hover plain parent row closes stale child") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  const int ox = 400, oy = 200;
  server.injectPointerMotionForTest(ox, oy);
  server.injectPointerButtonForTest(BTN_RIGHT, /*pressed=*/true);
  REQUIRE(server.menuOpenForTest());

  // Open child by hovering Workspaces (index 3).
  server.injectPointerMotionForTest(ox + 30, itemCentreY(oy, 3));
  Menu *root = server.rootMenuForTest();
  REQUIRE(root != nullptr);
  REQUIRE(root->submenuOpenForTest());

  // Hover "foot" (index 0) — plain row in the parent: child should close.
  server.injectPointerMotionForTest(ox + 30, itemCentreY(oy, 0));
  CHECK_FALSE(root->submenuOpenForTest());
  CHECK(server.menuOpenForTest());   // root menu itself stays open
}

// F3.5-c: clicking outside the whole chain while child is open closes everything.
TEST_CASE("F3.5 outside-click closes the whole chain") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  const int ox = 400, oy = 200;
  server.injectPointerMotionForTest(ox, oy);
  server.injectPointerButtonForTest(BTN_RIGHT, /*pressed=*/true);
  REQUIRE(server.menuOpenForTest());

  // Open child by hovering Workspaces (index 3).
  server.injectPointerMotionForTest(ox + 30, itemCentreY(oy, 3));
  REQUIRE(server.rootMenuForTest() != nullptr);
  REQUIRE(server.rootMenuForTest()->submenuOpenForTest());

  // Left-press far outside both menus — should dismiss the whole chain.
  server.injectPointerMotionForTest(1100, 600);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);

  CHECK_FALSE(server.menuOpenForTest());
}

// F3.5-d: pressing Escape while the child is open closes the whole chain.
TEST_CASE("F3.5 Escape from child closes the whole chain") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  const int ox = 400, oy = 200;
  server.injectPointerMotionForTest(ox, oy);
  server.injectPointerButtonForTest(BTN_RIGHT, /*pressed=*/true);
  REQUIRE(server.menuOpenForTest());

  // Open child by hovering Workspaces (index 3).
  server.injectPointerMotionForTest(ox + 30, itemCentreY(oy, 3));
  REQUIRE(server.rootMenuForTest() != nullptr);
  REQUIRE(server.rootMenuForTest()->submenuOpenForTest());

  // Press Escape — closes the whole chain.
  server.injectKeyForTest(XKB_KEY_Escape, 0, true);

  CHECK_FALSE(server.menuOpenForTest());
}
