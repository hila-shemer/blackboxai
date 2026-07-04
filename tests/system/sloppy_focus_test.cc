// Focus-follows-mouse, default-on. The refocus rides onPointerMotion's existing
// early-returns, so the three trap cases are: motion while LOCKED changes
// nothing, motion during an OPEN MENU changes nothing, motion during an ALT-TAB
// preview doesn't scramble the frozen ring. Two clients, non-overlapping.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>  // BTN_RIGHT

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
  }
  View *mapAt(Server &s, test::TestClient &c, int x, int y) {
    REQUIRE(c.ok());
    // Wait for a NEW view (count grows) AND for it to map - with two clients the
    // previous client's View is still views.back() until this one is created, so
    // a bare back()->isMapped() would return the wrong (stale) View.
    const size_t n0 = s.viewsForTest().size();
    auto mapped = [&] { const auto &v = s.viewsForTest();
                        return v.size() > n0 && v.back()->isMapped(); };
    for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); s.dispatch(); c.pump(); }
    REQUIRE(mapped());
    View *v = s.viewsForTest().back().get();
    v->setPosition(x, y);
    for (int i = 0; i < 30; ++i) { c.flush(); s.dispatch(); c.pump(); }
    return v;
  }
  // Client area of an SSD frame at (fx,fy): x in [fx+1, ...), y in [fy+23, ...).
  int clientX(View *v) { return v->x() + 30; }
  int clientY(View *v) { return v->y() + 40; }
}

TEST_CASE("hovering a window's client area focuses it (sloppy default-on)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);   // no rc -> SloppyFocus default
  boot(server);
  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *a = mapAt(server, ca, 100, 100);
  test::TestClient cb(server.socketName(), 0xFF0000FFu, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *b = mapAt(server, cb, 100, 400);   // B mapped last -> focused (focusNewWindows)
  REQUIRE(server.focusedViewForTest() == b);

  server.injectPointerMotionForTest(clientX(a), clientY(a));
  for (int i = 0; i < 10; ++i) { ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump(); }
  CHECK(server.focusedViewForTest() == a);    // followed the mouse
}

TEST_CASE("motion while LOCKED does not change focus") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *a = mapAt(server, ca, 100, 100);
  server.lockForTest();
  REQUIRE(server.focusedViewForTest() == nullptr);   // lock parks focus
  server.injectPointerMotionForTest(clientX(a), clientY(a));
  for (int i = 0; i < 10; ++i) { ca.flush(); server.dispatch(); ca.pump(); }
  CHECK(server.focusedViewForTest() == nullptr);     // still parked
}

TEST_CASE("motion during an open modal menu does not change focus") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *a = mapAt(server, ca, 100, 100);
  test::TestClient cb(server.socketName(), 0xFF0000FFu, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *b = mapAt(server, cb, 100, 400);
  server.focusViewForTest(b);

  // Right-click the bare desktop to open the root menu, then wander over A.
  server.injectPointerMotionForTest(700, 300);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  REQUIRE(server.menuOpenForTest());
  server.injectPointerMotionForTest(clientX(a), clientY(a));
  for (int i = 0; i < 10; ++i) { ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump(); }
  CHECK(server.focusedViewForTest() == b);   // menu is modal; focus unmoved
  server.injectKeyForTest(XKB_KEY_Escape, 0, true);
}

TEST_CASE("motion during an alt-tab preview does not fight the cycle") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *a = mapAt(server, ca, 100, 100);
  test::TestClient cb(server.socketName(), 0xFF0000FFu, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *b = mapAt(server, cb, 100, 400);   // b focused, MRU front

  server.injectKeyForTest(XKB_KEY_Tab, WLR_MODIFIER_ALT, true);   // preview -> a
  REQUIRE(server.cyclingForTest());
  const View *preview = server.focusedViewForTest();
  // A stray hover over B mid-cycle must NOT refocus B and scramble the ring.
  server.injectPointerMotionForTest(clientX(b), clientY(b));
  for (int i = 0; i < 5; ++i) { ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump(); }
  CHECK(server.focusedViewForTest() == preview);   // cycle preview intact
  CHECK(server.cyclingForTest());
  server.injectKeyForTest(XKB_KEY_Alt_L, 0, false); // commit
}
