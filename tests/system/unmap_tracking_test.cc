// Unmap tracking (lifetime-audit findings, 2026-07-10). A client can unmap the
// xdg way - wl_surface.attach(NULL) + commit - while keeping the role alive:
// wlroots resets the xdg surface (initialized=false) and the View survives to
// possibly re-map. Before these fixes NOTHING told the Server, so focused_view,
// the alt-tab ring, and open menu targets all kept naming the unmapped view,
// and the next set_activated/configure on the reset surface tripped wlroots'
// initialized assert: SIGABRT, session gone.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
  }
  void pump(Server &s, test::TestClient &c, int n = 40) {
    for (int i = 0; i < n; ++i) { c.flush(); s.dispatch(); c.pump(); }
  }
  // expect = the view count once THIS client's toplevel is in - back() alone
  // would return the previous client's already-mapped view instantly.
  View *mapOne(Server &s, test::TestClient &c, int x, int y, size_t expect) {
    auto mapped = [&] { const auto &v = s.viewsForTest();
                        return v.size() >= expect && v.back()->isMapped(); };
    for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); s.dispatch(); c.pump(); }
    REQUIRE(mapped());
    View *v = s.viewsForTest().back().get();
    v->setPosition(x, y);
    pump(s, c);
    return v;
  }
}

TEST_CASE("focused client unmapping hands focus off; the next transition survives") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150);
  test::TestClient b(server.socketName(), 0xFF00FF00u, 200, 150);
  REQUIRE(a.ok()); REQUIRE(b.ok());
  View *va = mapOne(server, a, 100, 100, 1);
  View *vb = mapOne(server, b, 500, 100, 2);
  server.focusViewForTest(va);
  REQUIRE(server.focusedViewForTest() == va);

  a.attachNullBuffer();
  auto unmapped = [&] { return !va->isMapped(); };
  for (int i = 0; i < 200 && !unmapped(); ++i)
    { a.flush(); b.flush(); server.dispatch(); a.pump(); b.pump(); }
  REQUIRE(unmapped());

  // The unmapped view must not be the seat's answer anymore.
  CHECK(server.focusedViewForTest() != va);

  // The abort site: a focus transition used to set_activated(false) on the
  // reset surface. Focus b explicitly and prove the server survives.
  server.focusViewForTest(vb);
  CHECK(server.focusedViewForTest() == vb);
  test::Frame f = test::captureFrame(server);
  CHECK(f.w == 1280u);
}

TEST_CASE("cycle survives its start window unmapping mid-cycle") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150);
  test::TestClient b(server.socketName(), 0xFF00FF00u, 200, 150);
  REQUIRE(a.ok()); REQUIRE(b.ok());
  View *va = mapOne(server, a, 100, 100, 1);
  View *vb = mapOne(server, b, 500, 100, 2);
  server.focusViewForTest(va);

  // Freeze the ring (Mod4+Tab, Super held).
  server.injectKeyForTest(XKB_KEY_Tab, WLR_MODIFIER_LOGO, true);
  server.injectKeyForTest(XKB_KEY_Tab, WLR_MODIFIER_LOGO, false);

  a.attachNullBuffer();
  auto unmapped = [&] { return !va->isMapped(); };
  for (int i = 0; i < 200 && !unmapped(); ++i)
    { a.flush(); b.flush(); server.dispatch(); a.pump(); b.pump(); }
  REQUIRE(unmapped());

  // Escape used to cancelCycle -> focusView(the unmapped start) -> SIGABRT.
  server.injectKeyForTest(XKB_KEY_Escape, WLR_MODIFIER_LOGO, true);
  server.injectKeyForTest(XKB_KEY_Escape, WLR_MODIFIER_LOGO, false);
  server.injectKeyForTest(XKB_KEY_Super_L, 0, false);
  CHECK(server.focusedViewForTest() == vb);
  test::Frame f = test::captureFrame(server);
  CHECK(f.w == 1280u);
}

TEST_CASE("an open Windowmenu closes when its target unmaps") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  REQUIRE(a.ok());
  View *va = mapOne(server, a, 300, 200, 1);

  server.injectPointerMotionForTest(va->x() + 100, va->y() + 3);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  REQUIRE(server.menuOpenForTest());
  server.injectPointerButtonForTest(BTN_RIGHT, false);

  // Target unmaps under the open menu: its baked View target is now a landmine
  // (MaximizeToggle would configure the reset surface). The menu must close.
  a.attachNullBuffer();
  auto unmapped = [&] { return !va->isMapped(); };
  for (int i = 0; i < 200 && !unmapped(); ++i)
    { a.flush(); server.dispatch(); a.pump(); }
  REQUIRE(unmapped());
  CHECK_FALSE(server.menuOpenForTest());
  test::Frame f = test::captureFrame(server);
  CHECK(f.w == 1280u);
}
