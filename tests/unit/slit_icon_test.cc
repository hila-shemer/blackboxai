// The icon-cell fallback ladder, pure half: bus pixmap (premultiplied +
// nearest-scaled) and the placeholder ring. The theme-PNG rung joins in a
// later task; the system golden covers compositing.
#include <doctest/doctest.h>
#include "Slit.hh"
#include "Screenshot.hh"

#include <cstdlib>
#include <fstream>

using namespace bbai;
using bbai::screenshot::encodePng;   // plan cited bbai::encodePng; it lives in bbai::screenshot

TEST_CASE("cellPixels: bus pixmap is premultiplied and nearest-scaled to the slot") {
  sni::Item it;
  sni::IconFrame f;
  f.width = 2; f.height = 2;
  // Network-order ARGB bytes: red, green, blue, semi-transparent (the mock's
  // kSniMockIcon layout - same alpha case the golden exercises).
  f.data = {0xff, 0xff, 0x00, 0x00,  0xff, 0x00, 0xff, 0x00,
            0xff, 0x00, 0x00, 0xff,  0x80, 0x10, 0x20, 0x30};
  it.icon_pixmaps.push_back(f);

  std::vector<uint32_t> px = sliticon::cellPixels(it, 24);
  REQUIRE(px.size() == 24u * 24u);
  CHECK(px[0] == 0xFFFF0000u);                    // top-left quadrant: red
  CHECK(px[23] == 0xFF00FF00u);                   // top-right: green
  CHECK(px[23u * 24u] == 0xFF0000FFu);            // bottom-left: blue
  CHECK(px[23u * 24u + 23u] == 0x80081018u);      // premultiplied (16,32,48 @ a=128)
}

TEST_CASE("cellPixels: no usable pixmap -> placeholder ring, transparent body") {
  sni::Item it;                                    // no pixmaps, no icon_name
  std::vector<uint32_t> px = sliticon::cellPixels(it, 24);
  REQUIRE(px.size() == 24u * 24u);
  CHECK(px[0] == 0u);                              // corner transparent
  CHECK(px[4u * 24u + 4u] == 0xFFAAAAAAu);         // ring pixel
  CHECK(px[12u * 24u + 12u] == 0u);                // center transparent
}

TEST_CASE("themeCandidates: IconThemePath first, hicolor sizes, pixmaps last") {
  auto c = sliticon::themeCandidates("nm-applet", "/tmp/theme");
  REQUIRE(c.size() == 10);
  CHECK(c[0] == "/tmp/theme/nm-applet.png");
  CHECK(c[1] == "/tmp/theme/hicolor/24x24/apps/nm-applet.png");
  CHECK(c[4] == "/tmp/theme/hicolor/48x48/apps/nm-applet.png");
  CHECK(c[5] == "/usr/share/icons/hicolor/24x24/apps/nm-applet.png");
  CHECK(c[9] == "/usr/share/pixmaps/nm-applet.png");
  // No IconThemePath -> system dirs only.
  CHECK(sliticon::themeCandidates("x", "").size() == 5);
}

TEST_CASE("decodePng round-trips encodePng, incl. straight alpha; missing = empty") {
  char tmpl[] = "/tmp/bbai-slit-icon-XXXXXX";
  REQUIRE(mkdtemp(tmpl) != nullptr);
  const std::string path = std::string(tmpl) + "/i.png";
  const std::vector<uint32_t> src{0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0x80102030u};
  const std::vector<uint8_t> png = encodePng(src, 2, 2);
  REQUIRE(!png.empty());
  {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(png.data()),
            static_cast<std::streamsize>(png.size()));
  }
  int w = 0, h = 0;
  std::vector<uint32_t> px = sliticon::decodePng(path, w, h);
  CHECK(w == 2); CHECK(h == 2);
  CHECK(px == src);                                   // PNG RGBA8 is lossless
  CHECK(sliticon::decodePng(path + ".nope", w, h).empty());
}

TEST_CASE("cellPixels: pixmap-less item decodes its theme PNG (premultiplied)") {
  char tmpl[] = "/tmp/bbai-slit-theme-XXXXXX";
  REQUIRE(mkdtemp(tmpl) != nullptr);
  const std::vector<uint32_t> src(4, 0x80FF0000u);    // 2x2, all semi-red
  const std::vector<uint8_t> png = encodePng(src, 2, 2);
  {
    std::ofstream f(std::string(tmpl) + "/tray.png", std::ios::binary);
    f.write(reinterpret_cast<const char *>(png.data()),
            static_cast<std::streamsize>(png.size()));
  }
  sni::Item it;
  it.icon_name = "tray";
  it.icon_theme_path = tmpl;
  std::vector<uint32_t> px = sliticon::cellPixels(it, 24);
  CHECK(px[0] == 0x80800000u);                        // decoded AND premultiplied
  // Undecodable name still lands on the placeholder, never a crash.
  it.icon_name = "no-such-icon";
  px = sliticon::cellPixels(it, 24);
  CHECK(px[4u * 24u + 4u] == 0xFFAAAAAAu);
}
