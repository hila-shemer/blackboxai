#include "Screenshot.hh"

#include <png.h>
#include <drm_fourcc.h>

#include <csetjmp>

namespace bbai::screenshot {

  namespace {
    void appendToVector(png_structp png, png_bytep data, png_size_t len) {
      auto *out = static_cast<std::vector<uint8_t> *>(png_get_io_ptr(png));
      out->insert(out->end(), data, data + len);
    }
  } // namespace

  std::vector<uint8_t> encodePng(const std::vector<uint32_t> &px, int w, int h) {
    std::vector<uint8_t> out;
    if (w <= 0 || h <= 0 || px.size() < static_cast<size_t>(w) * h) return out;

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) return out;
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_write_struct(&png, nullptr); return out; }
    // libpng error path. Keep no non-trivial-dtor locals live across this setjmp
    // except `out`, which we clear on error (mirrors the harness writePNG).
    if (setjmp(png_jmpbuf(png))) {
      png_destroy_write_struct(&png, &info);
      out.clear();
      return out;
    }
    png_set_write_fn(png, &out, appendToVector, nullptr);
    png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    std::vector<png_byte> row(static_cast<size_t>(w) * 4);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const uint32_t p = px[static_cast<size_t>(y) * w + x];
        row[x * 4 + 0] = (p >> 16) & 0xFF;  // R
        row[x * 4 + 1] = (p >> 8) & 0xFF;   // G
        row[x * 4 + 2] = p & 0xFF;          // B
        row[x * 4 + 3] = (p >> 24) & 0xFF;  // A
      }
      png_write_row(png, row.data());
    }
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    return out;
  }

  std::vector<uint32_t> captureRegion(wlr_scene_output *, wlr_renderer *,
                                      Rect, int &outW, int &outH) {
    outW = outH = 0;   // Task 3 fills this in.
    return {};
  }

} // namespace bbai::screenshot
