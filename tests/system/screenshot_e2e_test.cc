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

// Decode an in-memory PNG into RGBA rows so the e2e can assert real content, not
// just the magic bytes.
namespace {
  struct MemReader { const uint8_t *p; size_t len, off; };
  void readCb(png_structp png, png_bytep out, png_size_t n) {
    auto *r = static_cast<MemReader *>(png_get_io_ptr(png));
    size_t take = n;
    if (r->off + take > r->len) take = r->len - r->off;
    memcpy(out, r->p + r->off, take);
    r->off += take;
  }
  // Decode; return width/height and the row-major RGBA bytes.
  std::vector<uint8_t> decodePng(const std::vector<uint8_t> &bytes, int &w, int &h) {
    png_structp p = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(p);
    MemReader mr{ bytes.data(), bytes.size(), 0 };
    png_set_read_fn(p, &mr, readCb);
    png_read_info(p, info);
    w = png_get_image_width(p, info);
    h = png_get_image_height(p, info);
    if (png_get_bit_depth(p, info) == 16) png_set_strip_16(p);
    png_set_filler(p, 0xFF, PNG_FILLER_AFTER);
    png_read_update_info(p, info);
    std::vector<uint8_t> px((size_t)w * h * 4);
    for (int y = 0; y < h; ++y) png_read_row(p, px.data() + (size_t)y * w * 4, nullptr);
    png_destroy_read_struct(&p, &info, nullptr);
    return px;
  }
}

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

  // Decode and prove the clipboard PNG is the SELECTED REGION, not just any PNG:
  // dimensions equal the 80x60 drag, and an in-window pixel is the client's red
  // at FULL brightness (proving the dim overlay was absent from the readback).
  int pw = 0, ph = 0;
  std::vector<uint8_t> px = decodePng(got, pw, ph);
  CHECK(pw == 80);
  CHECK(ph == 60);
  REQUIRE(px.size() == (size_t)pw * ph * 4);
  const size_t mid = ((size_t)(ph / 2) * pw + pw / 2) * 4;
  CHECK(px[mid + 0] == 0xFF);   // R - full red, not ~0.65x dimmed
  CHECK(px[mid + 1] == 0x00);   // G
  CHECK(px[mid + 2] == 0x00);   // B
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

TEST_CASE("a second screenshot replaces the selection without double-free") {
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

  auto shoot = [&](int dx) {
    const int x0 = v->x() + 1 + dx, y0 = v->y() + 23;
    server.injectKeyForTest(XKB_KEY_F7, WLR_MODIFIER_LOGO, true);
    server.injectPointerMotionForTest(x0 + 5, y0 + 5);
    server.injectPointerButtonForTest(BTN_LEFT, true);
    server.injectPointerMotionForTest(x0 + 55, y0 + 45);
    server.injectPointerButtonForTest(BTN_LEFT, false);
  };

  shoot(0);
  wlr_data_source *first = server.seatSelectionSourceForTest();
  REQUIRE(first != nullptr);
  // The second set_selection synchronously destroys the first ClipboardImage
  // (the allocate-new-then-free-old order means the pointers differ). An owning
  // unique_ptr would double-free here; the raw-pointer + wlroots-sole-deleter
  // design does not.
  shoot(10);
  wlr_data_source *second = server.seatSelectionSourceForTest();
  REQUIRE(second != nullptr);
  CHECK(second != first);
  CHECK(std::strcmp(server.seatSelectionMimeForTest(), "image/png") == 0);
}
