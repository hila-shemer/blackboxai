// Demo scenario: window_ops
// Shows maximize/restore/iconify/deiconify on a single SSD window.
// Asserts state at each step; dumps frames when BBAI_DEMO_DIR is set.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "DemoRecorder.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"
#include "Frame.hh"
#include "Menu.hh"
#include "Menu.geom.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>

using namespace bbai;

namespace {
  int itemY(int open_y, int title_h, int index) {
    return open_y + title_h + menu::kFrameMargin
         + index * menu::itemHeight(18, false)
         + menu::itemHeight(18, false) / 2;
  }
}

TEST_CASE("demo: window_ops — maximize, restore, iconify, deiconify via icon menu") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  test::DemoRecorder rec("window_ops");

  // Map one SSD client (red, 200x150, default position (160,120)).
  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  REQUIRE(ca.ok());

  auto mapped = [&] {
    const auto &v = server.viewsForTest();
    return !v.empty() && v[0]->isMapped();
  };
  for (int i = 0; i < 500 && !mapped(); ++i) { ca.flush(); server.dispatch(); ca.pump(); }
  REQUIRE(mapped());
  for (int i = 0; i < 30; ++i) { ca.flush(); server.dispatch(); ca.pump(); }

  View *A = server.viewsForTest()[0].get();

  rec.shot(server);

  // --- Focus A via titlebar click ---
  server.injectPointerMotionForTest(A->x() + 100, A->y() + 10);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
  REQUIRE(server.focusedViewForTest() == A);

  // --- Click the maximize button ---
  frame::Rect mb = frame::maximizeButton(A->contentWidth(), A->contentHeight());
  int mx = A->x() + mb.x + frame::kButtonWidth / 2;
  int my = A->y() + mb.y + frame::kButtonWidth / 2;
  server.injectPointerMotionForTest(mx, my);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);

  // Pump to let the client commit its new buffer size.
  for (int i = 0; i < 30; ++i) { ca.flush(); server.dispatch(); ca.pump(); }

  CHECK(A->isMaximized());
  rec.shot(server, 3);

  // --- Click maximize again to restore ---
  frame::Rect mb2 = frame::maximizeButton(A->contentWidth(), A->contentHeight());
  int mx2 = A->x() + mb2.x + frame::kButtonWidth / 2;
  int my2 = A->y() + mb2.y + frame::kButtonWidth / 2;
  server.injectPointerMotionForTest(mx2, my2);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);
  for (int i = 0; i < 30; ++i) { ca.flush(); server.dispatch(); ca.pump(); }

  CHECK_FALSE(A->isMaximized());
  rec.shot(server, 3);

  // --- Click the iconify button ---
  frame::Rect ib = frame::iconifyButton(A->contentWidth(), A->contentHeight());
  int ibx = A->x() + ib.x + frame::kButtonWidth / 2;
  int iby = A->y() + ib.y + frame::kButtonWidth / 2;
  server.injectPointerMotionForTest(ibx, iby);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);

  CHECK(A->isIconified());
  CHECK_FALSE(A->visible());
  rec.shot(server, 3);

  // --- Open the icon menu via Mod4+Alt+T ---
  server.injectKeyForTest(XKB_KEY_t, WLR_MODIFIER_LOGO | WLR_MODIFIER_ALT, /*pressed=*/true);
  CHECK(server.menuOpenForTest());
  REQUIRE(server.rootMenuForTest() != nullptr);
  rec.shot(server, 2);

  // --- Click A's entry (item 0) to deiconify ---
  // The icon menu opens at a well-known position set by openIconMenuForTest.
  // Use the menu's actual rect origin for computing item Y.
  Menu *m = server.rootMenuForTest();
  REQUIRE(m != nullptr);
  REQUIRE(m->itemCount() >= 1);
  const int iy = itemY(m->rectYForTest(), menu::titleHeight(18), 0);
  const int ix = m->rectXForTest() + 30;
  server.injectPointerMotionForTest(ix, iy);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);

  CHECK_FALSE(server.menuOpenForTest());
  CHECK_FALSE(A->isIconified());
  CHECK(A->visible());
  rec.shot(server, 3);
}
