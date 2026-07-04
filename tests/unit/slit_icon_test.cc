// The icon-cell fallback ladder, pure half: bus pixmap (premultiplied +
// nearest-scaled) and the placeholder ring. The theme-PNG rung joins in a
// later task; the system golden covers compositing.
#include <doctest/doctest.h>
#include "Slit.hh"

using namespace bbai;

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
