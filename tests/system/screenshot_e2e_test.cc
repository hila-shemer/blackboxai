#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <cstring>
#include <linux/input-event-codes.h>
#include <png.h>

using namespace bbai;

static void mapOne(Server &s, test::TestClient &c) {
  auto mapped = [&] { const auto &v = s.viewsForTest(); return !v.empty() && v[0]->isMapped(); };
  for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); s.dispatch(); c.pump(); }
  for (int i = 0; i < 30; ++i) { c.flush(); s.dispatch(); c.pump(); }
}

TEST_CASE("Super+F7 drag-release puts an image/png of the region on the clipboard") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i) server.dispatch();

  test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  REQUIRE(c.ok());
  mapOne(server, c);
  View *v = server.viewsForTest()[0].get();
  const int x0 = v->x() + 1, y0 = v->y() + 23;   // inside client area, below titlebar

  server.injectKeyForTest(XKB_KEY_F7, WLR_MODIFIER_LOGO, true);
  server.injectPointerMotionForTest(x0 + 5, y0 + 5);
  server.injectPointerButtonForTest(BTN_LEFT, true);
  server.injectPointerMotionForTest(x0 + 85, y0 + 65);    // enclose ~80x60 of the window
  server.injectPointerButtonForTest(BTN_LEFT, false);

  CHECK_FALSE(server.screenshotActiveForTest());
  CHECK_FALSE(server.screenshotOverlayActiveForTest());
  REQUIRE(server.seatSelectionMimeForTest() != nullptr);
  CHECK(std::strcmp(server.seatSelectionMimeForTest(), "image/png") == 0);

  // Drive the owned source's writer over a pipe; assert it decodes to a PNG.
  wlr_data_source *src = server.seatSelectionSourceForTest();
  REQUIRE(src != nullptr);
  int fds[2]; REQUIRE(pipe(fds) == 0);
  fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
  src->impl->send(src, "image/png", fds[1]);
  std::vector<uint8_t> got;
  for (int i = 0; i < 2000; ++i) {
    server.dispatch();
    uint8_t buf[4096]; ssize_t n = read(fds[0], buf, sizeof buf);
    if (n > 0) got.insert(got.end(), buf, buf + n);
    else if (n == 0) break;
  }
  close(fds[0]);
  REQUIRE(got.size() > 8);
  CHECK(png_sig_cmp(got.data(), 0, 8) == 0);
}

TEST_CASE("a sub-pixel drag leaves the clipboard untouched") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i) server.dispatch();
  server.injectKeyForTest(XKB_KEY_F7, WLR_MODIFIER_LOGO, true);
  server.injectPointerMotionForTest(300, 300);
  server.injectPointerButtonForTest(BTN_LEFT, true);
  server.injectPointerMotionForTest(301, 301);
  server.injectPointerButtonForTest(BTN_LEFT, false);
  CHECK(server.seatSelectionSourceForTest() == nullptr);   // no selection set
}
