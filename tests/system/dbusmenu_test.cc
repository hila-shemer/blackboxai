// The com.canonical.dbusmenu client end-to-end against a forked mock that
// serves a 3-level tree at /MenuBar. Covers: recursionDepth=-1 full fetch and
// the (ia{sv}av) recursive parse into a MenuItem tree; separator/toggle/
// disabled classification; visible=false filtering; underscore-mnemonic
// stripping; unknown-vendor-key skip. dbus-run-session (private bus) +
// text_env (frames carry toolbar text; click coords ride the pinned font).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "SniHost.hh"
#include "SniMockItem.hh"
#include "Menu.hh"
#include "Text.hh"

#include <functional>
#include <unistd.h>
#include <linux/input-event-codes.h>

using namespace bbai;

namespace {
  void boot(Server &server) {
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
      server.dispatch();
    REQUIRE(server.titleFont()->height() == 18);
  }
  bool pumpUntil(Server &s, std::function<bool()> done, int tries = 1000) {
    for (int i = 0; i < tries && !done(); ++i) { s.dispatch(); usleep(3000); }
    return done();
  }
  // Bring up a Host + a with_menu mock, wait for the item to materialize, hand
  // back its Item (service + menu_path filled by the Host from the mock).
  sni::Item bringUpMenuItem(Server &server, test::SniMockChild &mock) {
    server.createSniHostForTest();
    REQUIRE(server.sniHostForTest()->ok());
    auto pump = [&]{ server.dispatch(); };
    REQUIRE(mock.waitReport(5000, pump) == "registered");
    REQUIRE(pumpUntil(server, [&]{ return !server.sniHostForTest()->items().empty(); }));
    return server.sniHostForTest()->items()[0];
  }
}

TEST_CASE("deep tree: GetLayout(-1) parses 3 levels; toggle/disabled/hidden/underscore honored") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::SniMockChild mock(/*register_by_name=*/false, /*with_menu=*/true);
  REQUIRE(mock.ok());
  const sni::Item it = bringUpMenuItem(server, mock);
  REQUIRE_FALSE(it.menu_path.empty());

  server.openSniContextMenu(it, 200, 200);
  REQUIRE(pumpUntil(server, [&]{ return server.menuOpenForTest(); }));
  Menu *m = server.rootMenuForTest();

  // root children: File(submenu), sep, Notifications(checkmark on), Upgrade
  // (disabled); "Secret" (visible=false) is filtered out.
  REQUIRE(m->itemCount() == 4);
  CHECK(m->item(0).kind == MenuItem::Kind::Submenu);
  CHECK(m->item(0).label == bt::decodeUtf8("File"));          // "_File" stripped
  CHECK(m->item(1).separator());
  CHECK(m->item(2).label == bt::decodeUtf8("Notifications"));
  CHECK(m->item(2).checked);                                  // toggle-state 1
  CHECK_FALSE(m->item(3).enabled);                            // Upgrade disabled

  // Level 2 + 3: File -> Recent -> doc1/doc2.
  m->openSubmenuAt(0);
  REQUIRE(m->submenuOpenForTest());
  Menu *file = m->child();
  REQUIRE(file->itemCount() == 2);                            // New, Recent
  CHECK(file->item(0).label == bt::decodeUtf8("New"));
  CHECK(file->item(1).kind == MenuItem::Kind::Submenu);
  file->openSubmenuAt(1);
  REQUIRE(file->submenuOpenForTest());
  Menu *recent = file->child();
  REQUIRE(recent->itemCount() == 2);
  CHECK(recent->item(0).label == bt::decodeUtf8("doc1"));
  CHECK(recent->item(1).label == bt::decodeUtf8("doc2"));

  server.injectKeyForTest(XKB_KEY_Escape, 0, true);
  mock.quit();
}

TEST_CASE("clicking a dbusmenu leaf fires Event(clicked) with its id over the bus") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::SniMockChild mock(false, /*with_menu=*/true);
  REQUIRE(mock.ok());
  const sni::Item it = bringUpMenuItem(server, mock);
  auto pump = [&]{ server.dispatch(); };

  server.openSniContextMenu(it, 200, 200);
  REQUIRE(pumpUntil(server, [&]{ return server.menuOpenForTest(); }));
  // Drain the AboutToShow/GetLayout reports so waitReport lands on the Event.
  while (mock.waitReport(200, pump) != "") {}

  Menu *m = server.rootMenuForTest();
  m->openSubmenuAt(0);                         // File
  REQUIRE(m->submenuOpenForTest());
  Menu *file = m->child();
  // Click "New" (id 11) via the menu's own hit-test (accessor-derived coords).
  int y = -1;
  for (int yy = file->rectYForTest(); yy < file->rectYForTest() + 400; ++yy)
    if (file->itemIndexAtGlobal(file->rectXForTest() + 5, yy) == 0) { y = yy; break; }
  REQUIRE(y >= 0);
  server.injectPointerMotionForTest(file->rectXForTest() + 5, y);
  server.injectPointerButtonForTest(BTN_LEFT, true);

  CHECK_FALSE(server.menuOpenForTest());       // click closes the chain
  CHECK(mock.waitReport(5000, pump) == "Event 11 clicked");
  mock.quit();
}

TEST_CASE("LayoutUpdated refetches only on a revision bump (udiskie loop guard)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::SniMockChild mock(false, true);
  REQUIRE(mock.ok());
  const sni::Item it = bringUpMenuItem(server, mock);
  auto pump = [&]{ server.dispatch(); };

  server.openSniContextMenu(it, 200, 200);
  REQUIRE(pumpUntil(server, [&]{ return server.menuOpenForTest(); }));
  REQUIRE(mock.waitReport(5000, pump) == "AboutToShow 0");
  REQUIRE(mock.waitReport(5000, pump) == "GetLayout");     // the one initial fetch

  mock.emitLayoutUpdated(/*bump_revision=*/false);          // same revision
  CHECK(mock.waitReport(500, pump) == "");                  // NO refetch

  mock.emitLayoutUpdated(/*bump_revision=*/true);           // revision changed
  CHECK(mock.waitReport(5000, pump) == "GetLayout");        // refetched exactly once
  server.injectKeyForTest(XKB_KEY_Escape, 0, true);
  mock.quit();
}

TEST_CASE("a GetLayout reply arriving after close is dropped, not a crash") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::SniMockChild mock(false, true);
  REQUIRE(mock.ok());
  const sni::Item it = bringUpMenuItem(server, mock);

  server.openSniContextMenu(it, 200, 200);      // AboutToShow/GetLayout in flight
  server.closeMenus();                          // user moved on before the reply
  // Pump hard: the reply lands on the bus but its slot was unref'd with sni_menu_.
  for (int i = 0; i < 300; ++i) { server.dispatch(); usleep(2000); }
  CHECK_FALSE(server.menuOpenForTest());        // no stale menu appeared, no crash
  mock.quit();
}

TEST_CASE("a menu-less item falls back to the SNI ContextMenu proxy") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::SniMockChild mock;                      // default: advertises Menu, serves no dbusmenu
  REQUIRE(mock.ok());
  const sni::Item it = bringUpMenuItem(server, mock);
  REQUIRE_FALSE(it.menu_path.empty());          // Menu=/MenuBar, but nothing there
  auto pump = [&]{ server.dispatch(); };

  server.openSniContextMenu(it, 7, 9);          // GetLayout errors -> proxy contextMenu
  CHECK(mock.waitReport(5000, pump) == "ContextMenu 7 9");
  CHECK_FALSE(server.menuOpenForTest());         // no dbusmenu menu opened
  mock.quit();
}

TEST_CASE("dbusmenu cascade clamps within the head it opened on") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  server.addHeadlessOutputForTest(1280, 720);
  for (int i = 0; i < 50 && server.outputCountForTest() != 2; ++i) server.dispatch();
  REQUIRE(server.outputCountForTest() == 2);
  test::SniMockChild mock(false, true);
  REQUIRE(mock.ok());
  const sni::Item it = bringUpMenuItem(server, mock);

  // Open near head 2's right edge; the File cascade would overflow 2560 and
  // must clamp back onto head 2 (never snap to head 1).
  server.openSniContextMenu(it, 2400, 100);
  REQUIRE(pumpUntil(server, [&]{ return server.menuOpenForTest(); }));
  Menu *m = server.rootMenuForTest();
  CHECK(m->rectXForTest() >= 1280);
  m->openSubmenuAt(0);                            // File
  REQUIRE(m->submenuOpenForTest());
  CHECK(m->childRectXForTest() >= 1280);          // on head 2
  CHECK(m->childRectXForTest() < 2560);
  server.injectKeyForTest(XKB_KEY_Escape, 0, true);
  mock.quit();
}
