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

  std::vector<uint32_t> captureRegion(wlr_scene_output *so, wlr_renderer *renderer,
                                      Rect sel, int &outW, int &outH) {
    outW = outH = 0;
    if (!so || !so->output || !renderer) return {};
    sel = clampToOutput(sel, so->output->width, so->output->height);
    if (sel.w <= 0 || sel.h <= 0) return {};

    wlr_output_state st;
    wlr_output_state_init(&st);
    if (!wlr_scene_output_build_state(so, &st, nullptr)) {
      wlr_output_state_finish(&st);
      return {};
    }
    wlr_texture *tex = wlr_texture_from_buffer(renderer, st.buffer);
    if (!tex) { wlr_output_state_finish(&st); return {}; }

    std::vector<uint32_t> px(static_cast<size_t>(sel.w) * sel.h);
    // src_box is a const member -> build the whole options aggregate at once.
    const wlr_texture_read_pixels_options opts = {
      .data = px.data(),
      .format = DRM_FORMAT_ARGB8888,
      .stride = static_cast<uint32_t>(sel.w) * 4,
      .dst_x = 0,
      .dst_y = 0,
      .src_box = { .x = sel.x, .y = sel.y, .width = sel.w, .height = sel.h },
    };
    const bool ok = wlr_texture_read_pixels(tex, &opts);
    wlr_texture_destroy(tex);
    wlr_output_state_finish(&st);
    if (!ok) return {};

    for (uint32_t &p : px) p |= 0xFF000000u;   // opaque guard (ARGB read is already opaque)
    outW = sel.w;
    outH = sel.h;
    return px;
  }

} // namespace bbai::screenshot
