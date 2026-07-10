// Hostile-client teardown permutations (lifetime-hardening spec, Phase 1).
// A client destroys its protocol objects in scripted orders - legal and
// ILLEGAL - at assorted lifecycle stages, then pokes the corpse. The contract:
// legal orders must not crash the compositor; illegal orders must cost the
// CLIENT a protocol error, never the server. Every scenario ends with the
// server demonstrably alive (dispatching, compositing, view list consistent).
// Run this under ASAN (build-asan) to give the "must not crash" teeth.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>
#include <memory>
#include <vector>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>

using namespace bbai;
using Obj = test::TestClient::Obj;

namespace {

  // One teardown script step: destroy an object, or commit the wl_surface.
  struct Step { bool commit; Obj obj; };
  constexpr Step D(Obj o) { return {false, o}; }
  constexpr Step COMMIT() { return {true, Obj::Surface}; }

  struct Order { const char *name; std::vector<Step> steps; bool legal; };

  // Legal orders: decoration -> toplevel -> xdg_surface -> wl_surface, buffer
  // free-floating; plus the live-crash shape (role down, bare commit, rest).
  // Illegal orders: each violates one ordering rule; wlroots posts the error.
  const std::vector<Order> &orders() {
    static const std::vector<Order> o = {
      {"legal-canonical", {D(Obj::Decoration), D(Obj::Toplevel), D(Obj::XdgSurface),
                           D(Obj::Surface), D(Obj::Buffer)}, true},
      {"legal-buffer-first", {D(Obj::Buffer), D(Obj::Decoration), D(Obj::Toplevel),
                              D(Obj::XdgSurface), D(Obj::Surface)}, true},
      {"legal-role-down-commit", {D(Obj::Decoration), D(Obj::Toplevel), COMMIT(),
                                  D(Obj::XdgSurface), COMMIT(), D(Obj::Surface)}, true},
      {"illegal-surface-first", {D(Obj::Surface), D(Obj::Buffer)}, false},
      {"illegal-xdgsurf-before-toplevel", {D(Obj::Decoration), D(Obj::XdgSurface),
                                           D(Obj::Toplevel)}, false},
      {"illegal-toplevel-before-deco", {D(Obj::Toplevel), D(Obj::Decoration)}, false},
    };
    return o;
  }

  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
  }

  template <typename Cond>
  bool pumpUntil(Server &s, test::TestClient &c, Cond cond, int iters = 500) {
    for (int i = 0; i < iters; ++i) {
      if (cond()) return true;
      c.flush();
      s.dispatch();
      c.pump();
    }
    return cond();
  }

  bool mapped(Server &s) {
    const auto &v = s.viewsForTest();
    return !v.empty() && v.back()->isMapped();
  }

  // Run one teardown script, then poke the corpse and prove the server alive.
  void runScript(Server &server, test::TestClient &c, const Order &ord) {
    INFO("order: " << ord.name);
    for (const Step &st : ord.steps) {
      if (st.commit) c.commitBareSurface();
      else c.destroyOne(st.obj);
      for (int i = 0; i < 20; ++i) { c.flush(); server.dispatch(); c.pump(); }
    }
    // The view must be gone: full legal teardown destroys it, an illegal order
    // kills the whole client (resource cascade destroys the toplevel too).
    bool gone = pumpUntil(server, c, [&] { return server.viewsForTest().empty(); });
    CHECK(gone);
    if (!ord.legal)
      CHECK(c.errored());
    // Alive: still dispatching and compositing a full frame.
    for (int i = 0; i < 10; ++i) server.dispatch();
    test::Frame f = test::captureFrame(server);
    CHECK(f.w == 1280u);
    CHECK(f.h == 720u);
    CHECK(server.viewsForTest().empty());
  }

  std::unique_ptr<test::TestClient> makeMapped(Server &server) {
    auto c = std::make_unique<test::TestClient>(server.socketName(), 0xFFCC3300u,
                                                200, 150,
                                                test::TestClient::Deco::RequestSSD);
    REQUIRE(c->ok());
    REQUIRE(pumpUntil(server, *c, [&] { return mapped(server); }));
    server.viewsForTest().back()->setPosition(300, 200);
    for (int i = 0; i < 20; ++i) { c->flush(); server.dispatch(); c->pump(); }
    return c;
  }

} // namespace

TEST_CASE("teardown permutations: freshly created, before the first map") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  for (const Order &ord : orders()) {
    test::TestClient c(server.socketName(), 0xFFCC3300u, 200, 150,
                       test::TestClient::Deco::RequestSSD);
    REQUIRE(c.ok());
    REQUIRE(pumpUntil(server, c, [&] { return c.created(); }));
    // No wait for map - the teardown races the compositor's configure cycle.
    runScript(server, c, ord);
  }
}

TEST_CASE("teardown permutations: mapped") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  for (const Order &ord : orders()) {
    auto c = makeMapped(server);
    runScript(server, *c, ord);
  }
}

TEST_CASE("teardown permutations: mid titlebar-drag move grab") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  for (const Order &ord : orders()) {
    auto c = makeMapped(server);
    View *v = server.viewsForTest().back().get();
    // Grab: left-press on the titlebar, never released by the client.
    server.injectPointerMotionForTest(v->x() + 100, v->y() + 3);
    server.injectPointerButtonForTest(BTN_LEFT, true);
    runScript(server, *c, ord);
    server.injectPointerButtonForTest(BTN_LEFT, false);  // clear leftover press
  }
}

TEST_CASE("teardown permutations: fullscreen") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  for (const Order &ord : orders()) {
    auto c = makeMapped(server);
    c->setFullscreen(true);
    for (int i = 0; i < 40; ++i) { c->flush(); server.dispatch(); c->pump(); }
    runScript(server, *c, ord);
  }
}

TEST_CASE("teardown permutations: window parked on another workspace") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  for (const Order &ord : orders()) {
    auto c = makeMapped(server);
    server.injectKeyForTest(XKB_KEY_Right, WLR_MODIFIER_LOGO, true);   // ws+1
    server.injectKeyForTest(XKB_KEY_Right, WLR_MODIFIER_LOGO, false);
    for (int i = 0; i < 20; ++i) { c->flush(); server.dispatch(); c->pump(); }
    runScript(server, *c, ord);
    server.injectKeyForTest(XKB_KEY_Left, WLR_MODIFIER_LOGO, true);    // back
    server.injectKeyForTest(XKB_KEY_Left, WLR_MODIFIER_LOGO, false);
  }
}

TEST_CASE("teardown permutations: while its Windowmenu is open") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  REQUIRE(server.titleFont() != nullptr);   // menu rows need the pinned font
  for (const Order &ord : orders()) {
    auto c = makeMapped(server);
    View *v = server.viewsForTest().back().get();
    // Right-click the titlebar label: the modal Windowmenu holds this View.
    server.injectPointerMotionForTest(v->x() + 100, v->y() + 3);
    server.injectPointerButtonForTest(BTN_RIGHT, true);
    REQUIRE(server.menuOpenForTest());
    runScript(server, *c, ord);
    // The menu's target died under it - dismiss whatever is left.
    server.injectKeyForTest(XKB_KEY_Escape, 0, true);
    server.injectKeyForTest(XKB_KEY_Escape, 0, false);
    server.injectPointerButtonForTest(BTN_RIGHT, false);
    CHECK_FALSE(server.menuOpenForTest());
  }
}

TEST_CASE("teardown permutations: mid screenshot region-select") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  for (const Order &ord : orders()) {
    auto c = makeMapped(server);
    server.injectKeyForTest(XKB_KEY_F7, WLR_MODIFIER_LOGO, true);
    REQUIRE(server.screenshotActiveForTest());
    server.injectPointerMotionForTest(100, 100);
    server.injectPointerButtonForTest(BTN_LEFT, true);   // drag armed
    runScript(server, *c, ord);
    server.injectKeyForTest(XKB_KEY_Escape, 0, true);    // leave the mode
    server.injectKeyForTest(XKB_KEY_F7, WLR_MODIFIER_LOGO, false);
    server.injectPointerButtonForTest(BTN_LEFT, false);
    CHECK_FALSE(server.screenshotActiveForTest());
  }
}

TEST_CASE("destroying the previewed window mid alt-tab cycle") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  auto a = makeMapped(server);
  auto b = makeMapped(server);
  REQUIRE(server.viewsForTest().size() == 2);
  // Hold the cycle open (Mod4+Tab pressed, Mod4 held) - the ring previews b.
  server.injectKeyForTest(XKB_KEY_Tab, WLR_MODIFIER_LOGO, true);
  server.injectKeyForTest(XKB_KEY_Tab, WLR_MODIFIER_LOGO, false);
  b->closeWindow();
  bool one = pumpUntil(server, *a, [&] { return server.viewsForTest().size() == 1; });
  CHECK(one);
  // End the cycle; focus must land on a live view, not the freed one.
  server.injectKeyForTest(XKB_KEY_Super_L, 0, false);
  for (int i = 0; i < 10; ++i) server.dispatch();
  test::Frame f = test::captureFrame(server);
  CHECK(f.w == 1280u);
}

TEST_CASE("a copy with a forged serial is rejected, not honored") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  auto c = makeMapped(server);
  c->forceSerialForTest(0xDEADBEEFu);   // never issued by this compositor
  REQUIRE(c->copyToClipboard());
  for (int i = 0; i < 60; ++i) { c->flush(); server.dispatch(); c->pump(); }
  CHECK(server.seatSelectionSourceForTest() == nullptr);
}
