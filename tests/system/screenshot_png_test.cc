#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "Screenshot.hh"
#include <png.h>
#include <cstdio>
#include <cstring>

using namespace bbai::screenshot;

// Decode an in-memory PNG with libpng to verify the round trip.
namespace {
  struct MemReader { const uint8_t *p; size_t len, off; };
  void readCb(png_structp png, png_bytep out, png_size_t n) {
    auto *r = static_cast<MemReader *>(png_get_io_ptr(png));
    size_t take = n;
    if (r->off + take > r->len) take = r->len - r->off;
    memcpy(out, r->p + r->off, take);
    r->off += take;
  }
}

TEST_CASE("encodePng round-trips dimensions and corner pixels") {
  const int W = 4, H = 3;
  std::vector<uint32_t> in(W * H, 0xFF000000u);   // opaque black
  in[0]            = 0xFFFF0000u;                  // TL red
  in[W - 1]        = 0xFF00FF00u;                  // TR green
  in[(H - 1) * W]  = 0xFF0000FFu;                  // BL blue

  std::vector<uint8_t> png = encodePng(in, W, H);
  REQUIRE(png.size() > 8);
  CHECK(png_sig_cmp(png.data(), 0, 8) == 0);       // valid PNG magic

  // Decode back.
  png_structp p = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info = png_create_info_struct(p);
  MemReader mr{ png.data(), png.size(), 0 };
  png_set_read_fn(p, &mr, readCb);
  png_read_info(p, info);
  CHECK(png_get_image_width(p, info) == (png_uint_32)W);
  CHECK(png_get_image_height(p, info) == (png_uint_32)H);
  if (png_get_bit_depth(p, info) == 16) png_set_strip_16(p);
  png_set_filler(p, 0xFF, PNG_FILLER_AFTER);
  png_read_update_info(p, info);
  std::vector<png_byte> row((size_t)W * 4);
  png_read_row(p, row.data(), nullptr);            // first row
  CHECK(row[0] == 0xFF); CHECK(row[1] == 0x00); CHECK(row[2] == 0x00);   // TL red R,G,B
  CHECK(row[(W - 1) * 4 + 1] == 0xFF);             // TR green G
  png_destroy_read_struct(&p, &info, nullptr);
}
