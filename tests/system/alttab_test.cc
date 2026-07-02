// Alt-tab MRU cycle (spec docs/superpowers/specs/2026-06-28-alt-tab-mru-design.md).
// Drives the session state machine through the cycleForTest / commitCycleForTest
// / cancelCycleForTest seams — the same logic the CycleNext/CyclePrev bindings and
// the onModifiers commit / Escape cancel funnels reach at runtime. Asserts focus,
// MRU order, and cross-workspace commit. The device-only "starting modifier went
// up" edge is hand-verified (spec §4), not exercised here.
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
  // Pump until `want` clients are mapped, then settle. `extra` is a second client
  // whose events must also be serviced (else its map never completes).
  void pump(Server &s, test::TestClient &c, size_t want, test::TestClient *extra = nullptr) {
    for (int i = 0; i < 800 && s.viewsForTest().size() < want; ++i) {
      c.flush(); if (extra) extra->flush();
      s.dispatch();
      c.pump(); if (extra) extra->pump();
    }
    for (int i = 0; i < 40; ++i) {
      c.flush(); if (extra) extra->flush();
      s.dispatch();
      c.pump(); if (extra) extra->pump();
    }
  }
  void clickTitlebar(Server &s) {  // focus the topmost window under (260,130)
    s.injectPointerMotionForTest(260, 130);
    s.injectPointerButtonForTest(BTN_LEFT, true);
    s.injectPointerButtonForTest(BTN_LEFT, false);
  }
  Server *bootServer(Server &server) {
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
      server.dispatch();
    return &server;
  }
}

TEST_CASE("preview raises+focuses without reordering MRU; commit reorders") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootServer(server);

  // Three windows on ws0. New windows enter the MRU at front, so after A,B,C the
  // order is C,B,A; C is topmost, so a titlebar click focuses it.
  test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150);
  test::TestClient b(server.socketName(), 0xFF00FF00u, 200, 150);
  test::TestClient c(server.socketName(), 0xFF0000FFu, 200, 150);
  REQUIRE(a.ok()); REQUIRE(b.ok()); REQUIRE(c.ok());
  for (int i = 0; i < 1200 && server.viewsForTest().size() < 3; ++i) {
    a.flush(); b.flush(); c.flush(); server.dispatch(); a.pump(); b.pump(); c.pump();
  }
  for (int i = 0; i < 60; ++i) { a.flush(); b.flush(); c.flush(); server.dispatch(); a.pump(); b.pump(); c.pump(); }
  REQUIRE(server.viewsForTest().size() == 3);
  View *va = server.viewsForTest()[0].get();
  View *vb = server.viewsForTest()[1].get();
  View *vc = server.viewsForTest()[2].get();

  clickTitlebar(server);
  REQUIRE(server.focusedViewForTest() == vc);
  REQUIRE(server.mruForTest() == std::vector<View *>{vc, vb, va});

  // Forward one step: previews B (next-most-recent). Ring is frozen and MRU is
  // untouched — the preview must not scramble last-used order.
  server.cycleForTest(+1);
  CHECK(server.cyclingForTest());
  CHECK(server.focusedViewForTest() == vb);
  CHECK(server.mruForTest() == std::vector<View *>{vc, vb, va});

  // Commit: B moves to the MRU front, session ends.
  server.commitCycleForTest();
  CHECK_FALSE(server.cyclingForTest());
  CHECK(server.focusedViewForTest() == vb);
  CHECK(server.mruForTest() == std::vector<View *>{vb, vc, va});
}

TEST_CASE("wraparound then cancel restores the start window and leaves MRU intact") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootServer(server);

  test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150);
  test::TestClient b(server.socketName(), 0xFF00FF00u, 200, 150);
  test::TestClient c(server.socketName(), 0xFF0000FFu, 200, 150);
  REQUIRE(a.ok()); REQUIRE(b.ok()); REQUIRE(c.ok());
  for (int i = 0; i < 1200 && server.viewsForTest().size() < 3; ++i) {
    a.flush(); b.flush(); c.flush(); server.dispatch(); a.pump(); b.pump(); c.pump();
  }
  for (int i = 0; i < 60; ++i) { a.flush(); b.flush(); c.flush(); server.dispatch(); a.pump(); b.pump(); c.pump(); }
  REQUIRE(server.viewsForTest().size() == 3);
  View *va = server.viewsForTest()[0].get();
  View *vb = server.viewsForTest()[1].get();
  View *vc = server.viewsForTest()[2].get();
  clickTitlebar(server);
  REQUIRE(server.focusedViewForTest() == vc);   // MRU: C,B,A

  server.cycleForTest(+1);   // -> B
  server.cycleForTest(+1);   // -> A
  CHECK(server.focusedViewForTest() == va);
  server.cycleForTest(+1);   // wraps -> C
  CHECK(server.focusedViewForTest() == vc);

  // Escape mid-cycle: focus returns to the session-start window (C), MRU unchanged.
  server.cancelCycleForTest();
  CHECK_FALSE(server.cyclingForTest());
  CHECK(server.focusedViewForTest() == vc);
  CHECK(server.mruForTest() == std::vector<View *>{vc, vb, va});
}

TEST_CASE("committing onto a window on another workspace switches to it") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootServer(server);

  // A on ws0, focused.
  test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150);
  REQUIRE(a.ok());
  pump(server, a, 1);
  View *va = server.viewsForTest()[0].get();
  clickTitlebar(server);
  REQUIRE(server.focusedViewForTest() == va);

  // B on ws1, focused. A is now off-workspace but still mapped -> stays in the ring.
  server.setCurrentWorkspace(1);
  test::TestClient b(server.socketName(), 0xFF00FF00u, 200, 150);
  REQUIRE(b.ok());
  pump(server, b, 2, &a);
  REQUIRE(server.viewsForTest().size() == 2);
  View *vb = server.viewsForTest()[1].get();
  REQUIRE(vb->workspace() == 1);
  clickTitlebar(server);
  REQUIRE(server.focusedViewForTest() == vb);   // MRU: B,A ; current ws 1

  // Cycle from B to A (on ws0) and commit -> workspace follows the target.
  server.cycleForTest(+1);
  CHECK(server.focusedViewForTest() == va);
  server.commitCycleForTest();
  CHECK(server.currentWorkspaceForTest() == 0);
  CHECK(server.focusedViewForTest() == va);
  CHECK(server.mruForTest() == std::vector<View *>{va, vb});
}

TEST_CASE("cross-workspace commit keeps MRU order: only the target moves to front") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootServer(server);

  // X then A on ws0; click focuses A (topmost). MRU: A,X.
  test::TestClient x(server.socketName(), 0xFFFF0000u, 200, 150);
  test::TestClient a(server.socketName(), 0xFF00FF00u, 200, 150);
  REQUIRE(x.ok()); REQUIRE(a.ok());
  for (int i = 0; i < 1200 && server.viewsForTest().size() < 2; ++i) {
    x.flush(); a.flush(); server.dispatch(); x.pump(); a.pump();
  }
  for (int i = 0; i < 60; ++i) { x.flush(); a.flush(); server.dispatch(); x.pump(); a.pump(); }
  REQUIRE(server.viewsForTest().size() == 2);
  View *vx = server.viewsForTest()[0].get();
  View *va = server.viewsForTest()[1].get();
  clickTitlebar(server);
  REQUIRE(server.focusedViewForTest() == va);

  // B on ws1, focused. The switch away records ws0's remembered focus = A,
  // which is NOT the window we'll commit onto — the case that scrambles MRU if
  // the switch's focus-restore runs against the wrong view.
  server.setCurrentWorkspace(1);
  test::TestClient b(server.socketName(), 0xFF0000FFu, 200, 150);
  REQUIRE(b.ok());
  pump(server, b, 3, &a);
  REQUIRE(server.viewsForTest().size() == 3);
  View *vb = server.viewsForTest()[2].get();
  REQUIRE(vb->workspace() == 1);
  clickTitlebar(server);
  REQUIRE(server.focusedViewForTest() == vb);
  REQUIRE(server.mruForTest() == std::vector<View *>{vb, va, vx});

  // Cycle B -> A -> X and commit onto X (ws0). Only X may move to the MRU
  // front; B and A keep their relative order: X,B,A — not X,A,B.
  server.cycleForTest(+1);
  CHECK(server.focusedViewForTest() == va);
  server.cycleForTest(+1);
  CHECK(server.focusedViewForTest() == vx);
  server.commitCycleForTest();
  CHECK(server.currentWorkspaceForTest() == 0);
  CHECK(server.focusedViewForTest() == vx);
  CHECK(server.mruForTest() == std::vector<View *>{vx, vb, va});
}

TEST_CASE("cycling with no focused window lands on the MRU front first") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootServer(server);

  // A then B on ws1, B focused. Back on empty ws0 nothing is focused, but both
  // stay in the ring (mapped, not iconified).
  server.setCurrentWorkspace(1);
  test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150);
  test::TestClient b(server.socketName(), 0xFF00FF00u, 200, 150);
  REQUIRE(a.ok()); REQUIRE(b.ok());
  for (int i = 0; i < 1200 && server.viewsForTest().size() < 2; ++i) {
    a.flush(); b.flush(); server.dispatch(); a.pump(); b.pump();
  }
  for (int i = 0; i < 60; ++i) { a.flush(); b.flush(); server.dispatch(); a.pump(); b.pump(); }
  REQUIRE(server.viewsForTest().size() == 2);
  View *va = server.viewsForTest()[0].get();
  View *vb = server.viewsForTest()[1].get();
  clickTitlebar(server);
  REQUIRE(server.focusedViewForTest() == vb);   // MRU: B,A

  server.setCurrentWorkspace(0);
  REQUIRE(server.focusedViewForTest() == nullptr);

  // Forward from an unfocused state: the first candidate is the most-recently
  // used window (ring front), not the second one.
  server.cycleForTest(+1);
  CHECK(server.focusedViewForTest() == vb);
  server.commitCycleForTest();
  CHECK(server.currentWorkspaceForTest() == 1);
  CHECK(server.focusedViewForTest() == vb);
  (void)va;
}

TEST_CASE("the cycle is modal: other bound keys are swallowed while cycling") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootServer(server);

  test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150);
  test::TestClient b(server.socketName(), 0xFF00FF00u, 200, 150);
  REQUIRE(a.ok()); REQUIRE(b.ok());
  for (int i = 0; i < 1200 && server.viewsForTest().size() < 2; ++i) {
    a.flush(); b.flush(); server.dispatch(); a.pump(); b.pump();
  }
  for (int i = 0; i < 60; ++i) { a.flush(); b.flush(); server.dispatch(); a.pump(); b.pump(); }
  REQUIRE(server.viewsForTest().size() == 2);
  View *vb = server.viewsForTest()[1].get();
  clickTitlebar(server);
  REQUIRE(server.focusedViewForTest() == vb);

  const uint32_t SUPER = WLR_MODIFIER_LOGO;
  server.injectKeyForTest(XKB_KEY_Tab, SUPER, true);   // Super+Tab starts the cycle
  REQUIRE(server.cyclingForTest());

  // Super+Right (WorkspaceNext) and Super+space (OpenMenu) must not fire
  // mid-cycle — the session owns the keyboard like the other modal modes.
  server.injectKeyForTest(XKB_KEY_Right, SUPER, true);
  CHECK(server.currentWorkspaceForTest() == 0);
  CHECK(server.cyclingForTest());
  server.injectKeyForTest(XKB_KEY_space, SUPER, true);
  CHECK_FALSE(server.menuOpenForTest());
  CHECK(server.cyclingForTest());

  server.cancelCycleForTest();
}

TEST_CASE("a window turning invisible after the freeze is skipped, not previewed") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootServer(server);

  test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150);
  test::TestClient b(server.socketName(), 0xFF00FF00u, 200, 150);
  test::TestClient c(server.socketName(), 0xFF0000FFu, 200, 150);
  REQUIRE(a.ok()); REQUIRE(b.ok()); REQUIRE(c.ok());
  for (int i = 0; i < 1200 && server.viewsForTest().size() < 3; ++i) {
    a.flush(); b.flush(); c.flush(); server.dispatch(); a.pump(); b.pump(); c.pump();
  }
  for (int i = 0; i < 60; ++i) { a.flush(); b.flush(); c.flush(); server.dispatch(); a.pump(); b.pump(); c.pump(); }
  REQUIRE(server.viewsForTest().size() == 3);
  View *va = server.viewsForTest()[0].get();
  View *vc = server.viewsForTest()[2].get();
  clickTitlebar(server);
  REQUIRE(server.focusedViewForTest() == vc);   // MRU: C,B,A

  // Freeze the ring [C,B,A], preview B, then A goes invisible (same predicate
  // as an unmap): the next step must skip A and wrap to C.
  server.cycleForTest(+1);
  va->setIconified(true);
  server.cycleForTest(+1);
  CHECK(server.focusedViewForTest() == vc);
  CHECK(server.cyclingForTest());

  // Everything in the ring goes invisible -> stepping finds no candidate and
  // the session dissolves instead of previewing a hidden window.
  View *vb = server.viewsForTest()[1].get();
  vb->setIconified(true);
  vc->setIconified(true);
  server.cycleForTest(+1);
  CHECK_FALSE(server.cyclingForTest());
  va->setIconified(false);
  vb->setIconified(false);
  vc->setIconified(false);
}

TEST_CASE("a window closed mid-cycle is pruned from the frozen ring") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootServer(server);

  test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150);
  test::TestClient b(server.socketName(), 0xFF00FF00u, 200, 150);
  test::TestClient c(server.socketName(), 0xFF0000FFu, 200, 150);
  REQUIRE(a.ok()); REQUIRE(b.ok()); REQUIRE(c.ok());
  for (int i = 0; i < 1200 && server.viewsForTest().size() < 3; ++i) {
    a.flush(); b.flush(); c.flush(); server.dispatch(); a.pump(); b.pump(); c.pump();
  }
  for (int i = 0; i < 60; ++i) { a.flush(); b.flush(); c.flush(); server.dispatch(); a.pump(); b.pump(); c.pump(); }
  REQUIRE(server.viewsForTest().size() == 3);
  View *vb = server.viewsForTest()[1].get();
  View *vc = server.viewsForTest()[2].get();
  clickTitlebar(server);
  REQUIRE(server.focusedViewForTest() == vc);   // MRU: C,B,A

  server.cycleForTest(+1);   // preview B; ring frozen [C,B,A]
  REQUIRE(server.focusedViewForTest() == vb);

  // A dies mid-hold: removeView must drop it from the frozen ring so stepping
  // never dereferences the dead View.
  a.closeWindow();
  for (int i = 0; i < 200 && server.viewsForTest().size() > 2; ++i) {
    a.flush(); b.flush(); c.flush(); server.dispatch(); a.pump(); b.pump(); c.pump();
  }
  REQUIRE(server.viewsForTest().size() == 2);

  server.cycleForTest(+1);   // from B: skip the dead slot, wrap to C
  CHECK(server.focusedViewForTest() == vc);
  server.commitCycleForTest();
  CHECK(server.focusedViewForTest() == vc);
  CHECK(server.mruForTest() == std::vector<View *>{vc, vb});
}

TEST_CASE("the preview restacks: the previewed window rises to the top") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootServer(server);

  // Two same-geometry overlapping windows: A (red) below, B (green) on top.
  test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150);
  test::TestClient b(server.socketName(), 0xFF00FF00u, 200, 150);
  REQUIRE(a.ok()); REQUIRE(b.ok());
  for (int i = 0; i < 1200 && server.viewsForTest().size() < 2; ++i) {
    a.flush(); b.flush(); server.dispatch(); a.pump(); b.pump();
  }
  for (int i = 0; i < 60; ++i) { a.flush(); b.flush(); server.dispatch(); a.pump(); b.pump(); }
  REQUIRE(server.viewsForTest().size() == 2);
  View *va = server.viewsForTest()[0].get();
  clickTitlebar(server);   // focus B (topmost); MRU: B,A

  // Content probe inside the overlap (same point workspace_switch_test uses).
  auto centre = [&] { return test::captureFrame(server).pixels[218u * 1280 + 261] & 0x00FFFFFFu; };
  REQUIRE(centre() == 0x0000FF00u);   // B (green) covers A

  // Preview A: spec §2 — "the preview IS the real window raising+focusing."
  server.cycleForTest(+1);
  CHECK(server.focusedViewForTest() == va);
  CHECK(centre() == 0x00FF0000u);     // A (red) rose above B
  server.cancelCycleForTest();
}

TEST_CASE("the Alt+Tab key funnel starts/steps the cycle and Escape cancels it") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootServer(server);

  const uint32_t ALT = WLR_MODIFIER_ALT;
  const uint32_t SHIFT = WLR_MODIFIER_SHIFT;

  test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150);
  test::TestClient b(server.socketName(), 0xFF00FF00u, 200, 150);
  test::TestClient c(server.socketName(), 0xFF0000FFu, 200, 150);
  REQUIRE(a.ok()); REQUIRE(b.ok()); REQUIRE(c.ok());
  for (int i = 0; i < 1200 && server.viewsForTest().size() < 3; ++i) {
    a.flush(); b.flush(); c.flush(); server.dispatch(); a.pump(); b.pump(); c.pump();
  }
  for (int i = 0; i < 60; ++i) { a.flush(); b.flush(); c.flush(); server.dispatch(); a.pump(); b.pump(); c.pump(); }
  REQUIRE(server.viewsForTest().size() == 3);
  View *va = server.viewsForTest()[0].get();
  View *vb = server.viewsForTest()[1].get();
  View *vc = server.viewsForTest()[2].get();
  clickTitlebar(server);
  REQUIRE(server.focusedViewForTest() == vc);   // MRU: C,B,A

  // Alt+Tab through the real binding matcher -> CycleNext -> cycleStep(+1).
  server.injectKeyForTest(XKB_KEY_Tab, ALT, /*pressed=*/true);
  CHECK(server.lastActionForTest() == Action::CycleNext);
  CHECK(server.cyclingForTest());
  CHECK(server.focusedViewForTest() == vb);
  server.injectKeyForTest(XKB_KEY_Tab, ALT, true);   // step again -> A
  CHECK(server.focusedViewForTest() == va);

  // Escape through the funnel cancels + is consumed; focus returns to the start.
  server.injectKeyForTest(XKB_KEY_Escape, 0, true);
  CHECK_FALSE(server.cyclingForTest());
  CHECK(server.focusedViewForTest() == vc);
  CHECK(server.mruForTest() == std::vector<View *>{vc, vb, va});

  // Alt+Shift+Tab steps backward: from C (front) that wraps to A (least-recent).
  server.injectKeyForTest(XKB_KEY_Tab, ALT | SHIFT, true);
  CHECK(server.lastActionForTest() == Action::CyclePrev);
  CHECK(server.focusedViewForTest() == va);
  // Real hardware Shift+Tab emits ISO_Left_Tab — that row must also step back.
  server.injectKeyForTest(XKB_KEY_ISO_Left_Tab, ALT | SHIFT, true);
  CHECK(server.lastActionForTest() == Action::CyclePrev);
  CHECK(server.focusedViewForTest() == vb);   // A -> back one more -> B
  server.cancelCycleForTest();
}

TEST_CASE("a single visible window is nothing to cycle to") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootServer(server);

  test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150);
  REQUIRE(a.ok());
  pump(server, a, 1);
  View *va = server.viewsForTest()[0].get();
  clickTitlebar(server);
  REQUIRE(server.focusedViewForTest() == va);

  server.cycleForTest(+1);   // ring size 1 -> no-op session
  CHECK_FALSE(server.cyclingForTest());
  CHECK(server.focusedViewForTest() == va);
}
