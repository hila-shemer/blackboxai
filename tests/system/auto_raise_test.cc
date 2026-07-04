// AutoRaise: after focus-follows-mouse settles on a window, it raises after
// autoRaiseDelay (400ms default) - driven by the VirtualClock. ClickRaise:
// a click in a window raises it immediately. Both are sloppy sub-flags, so the
// rc sets SloppyFocus explicitly.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>
#include <linux/input-event-codes.h>

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
  }
  std::string writeRc(const std::string &body) {
    char t[] = "/tmp/bbai-raise-XXXXXX";
    REQUIRE(mkdtemp(t) != nullptr);
    std::string p = std::string(t) + "/rc";
    std::ofstream(p) << body;
    return p;
  }
  View *mapAt(Server &s, test::TestClient &c, int x, int y) {
    REQUIRE(c.ok());
    // Wait for a NEW view to map (two clients: the previous is still back()).
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
}

TEST_CASE("AutoRaise raises the hovered window after the delay") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  const std::string rc = writeRc("session.focusModel: SloppyFocus AutoRaise\n");
  Server server(/*headless=*/true, rc);
  boot(server);
  // A behind B (B mapped last, raised). Hover A: focus follows immediately, but
  // the RAISE waits for the timer.
  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *a = mapAt(server, ca, 150, 150);
  test::TestClient cb(server.socketName(), 0xFF0000FFu, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *b = mapAt(server, cb, 200, 200);

  server.injectPointerMotionForTest(a->x() + 30, a->y() + 40);
  for (int i = 0; i < 10; ++i) { ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump(); }
  REQUIRE(server.focusedViewForTest() == a);
  CHECK_FALSE(server.isTopmostForTest(a));    // focused but not yet raised
  server.advanceClockForTest(1);              // 1s > 400ms one-shot -> fires
  CHECK(server.isTopmostForTest(a));          // now raised
}

TEST_CASE("closing a window with autoraise armed disarms the pending raise") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  const std::string rc = writeRc("session.focusModel: SloppyFocus AutoRaise\n");
  Server server(/*headless=*/true, rc);
  boot(server);
  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *a = mapAt(server, ca, 150, 150);
  test::TestClient cb(server.socketName(), 0xFF0000FFu, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *b = mapAt(server, cb, 400, 400);

  // Hover A: focus follows and the one-shot autoraise timer arms with A as the
  // sole handle (autoraise_pending_).
  server.injectPointerMotionForTest(a->x() + 30, a->y() + 40);
  for (int i = 0; i < 10; ++i) { ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump(); }
  REQUIRE(server.focusedViewForTest() == a);
  REQUIRE(server.autoRaisePendingForTest() == a);
  REQUIRE(server.autoRaiseTimerArmedForTest());

  // Close A before the delay elapses. removeView (driven by the surface-destroy
  // handler) must scrub the armed pointer and stop the timer, else
  // autoraise_pending_ dangles at freed memory with the one-shot still pending.
  const void *a_addr = a;
  ca.closeWindow();
  for (int i = 0; i < 60 && server.viewsForTest().size() > 1; ++i) {
    ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump();
  }
  REQUIRE(server.viewsForTest().size() == 1);   // A is gone
  CHECK(server.autoRaisePendingForTest() != a_addr);   // not left pointing at freed A
  CHECK(server.autoRaisePendingForTest() == nullptr);
  CHECK_FALSE(server.autoRaiseTimerArmedForTest());
  server.advanceClockForTest(1);   // must not fire a stale raise on freed memory
}

TEST_CASE("ClickRaise raises on a click in the window") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  const std::string rc = writeRc("session.focusModel: SloppyFocus ClickRaise\n");
  Server server(/*headless=*/true, rc);
  boot(server);
  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *a = mapAt(server, ca, 150, 150);
  test::TestClient cb(server.socketName(), 0xFF0000FFu, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *b = mapAt(server, cb, 200, 200);
  REQUIRE(server.isTopmostForTest(b));

  // Click in A's client area: focus + raise, immediately (no timer).
  server.injectPointerMotionForTest(a->x() + 30, a->y() + 40);
  server.injectPointerButtonForTest(BTN_LEFT, true);
  server.injectPointerButtonForTest(BTN_LEFT, false);
  for (int i = 0; i < 20; ++i) { ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump(); }
  CHECK(server.isTopmostForTest(a));
}
