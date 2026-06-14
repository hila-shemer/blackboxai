#include "HeadlessFixture.hh"
#include "Server.hh"
#include "wlr.hpp"

#include <png.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <sys/stat.h>

namespace bbai::test {

  Frame captureFirstFrame() {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);

    Server server(/*headless=*/true);
    if (!server.ok())
      throw std::runtime_error("headless Server failed to start");

    // The output is created synchronously by wlr_headless_add_output during
    // construction; pump a few iterations defensively in case anything defers.
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
      server.dispatch();
    wlr_scene_output *so = server.activeSceneOutputForTest();
    if (!so)
      throw std::runtime_error("no scene output after pumping the event loop");

    // Render the scene into a fresh output-state buffer (no page-flip needed).
    wlr_output_state st;
    wlr_output_state_init(&st);
    if (!wlr_scene_output_build_state(so, &st, nullptr)) {
      wlr_output_state_finish(&st);
      throw std::runtime_error("wlr_scene_output_build_state failed");
    }

    void *data = nullptr;
    uint32_t fmt = 0;
    size_t stride = 0;
    if (!st.buffer ||
        !wlr_buffer_begin_data_ptr_access(st.buffer, WLR_BUFFER_DATA_PTR_ACCESS_READ,
                                          &data, &fmt, &stride)) {
      wlr_output_state_finish(&st);
      throw std::runtime_error("begin_data_ptr_access on the rendered buffer failed");
    }

    Frame f;
    f.w = static_cast<uint32_t>(so->output->width);
    f.h = static_cast<uint32_t>(so->output->height);
    f.pixels.resize(static_cast<size_t>(f.w) * f.h);
    const uint8_t *src = static_cast<const uint8_t *>(data);
    for (uint32_t y = 0; y < f.h; ++y) {
      const uint32_t *row =
        reinterpret_cast<const uint32_t *>(src + static_cast<size_t>(y) * stride);
      for (uint32_t x = 0; x < f.w; ++x)
        f.pixels[static_cast<size_t>(y) * f.w + x] = 0xFF000000u | (row[x] & 0x00FFFFFFu);
    }

    wlr_buffer_end_data_ptr_access(st.buffer);
    wlr_output_state_finish(&st);
    return f;
  }

  // ---- libpng helpers (RGBA8) -------------------------------------------------

  static bool writePNG(const std::string &path, uint32_t w, uint32_t h,
                       const std::vector<uint32_t> &px) {
    FILE *fp = std::fopen(path.c_str(), "wb");
    if (!fp) return false;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) { std::fclose(fp); return false; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_write_struct(&png, nullptr); std::fclose(fp); return false; }
    if (setjmp(png_jmpbuf(png))) {
      png_destroy_write_struct(&png, &info); std::fclose(fp); return false;
    }
    png_init_io(png, fp);
    png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    std::vector<png_byte> row(static_cast<size_t>(w) * 4);
    for (uint32_t y = 0; y < h; ++y) {
      for (uint32_t x = 0; x < w; ++x) {
        uint32_t p = px[static_cast<size_t>(y) * w + x];
        row[x * 4 + 0] = (p >> 16) & 0xFF; // R
        row[x * 4 + 1] = (p >> 8) & 0xFF;  // G
        row[x * 4 + 2] = p & 0xFF;         // B
        row[x * 4 + 3] = (p >> 24) & 0xFF; // A
      }
      png_write_row(png, row.data());
    }
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    std::fclose(fp);
    return true;
  }

  static bool readPNG(const std::string &path, uint32_t &w, uint32_t &h,
                      std::vector<uint32_t> &px) {
    FILE *fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) { std::fclose(fp); return false; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, nullptr, nullptr); std::fclose(fp); return false; }
    if (setjmp(png_jmpbuf(png))) {
      png_destroy_read_struct(&png, &info, nullptr); std::fclose(fp); return false;
    }
    png_init_io(png, fp);
    png_read_info(png, info);
    w = png_get_image_width(png, info);
    h = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);
    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
      png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
      png_set_gray_to_rgb(png);
    png_read_update_info(png, info);
    px.resize(static_cast<size_t>(w) * h);
    std::vector<png_byte> row(static_cast<size_t>(w) * 4);
    for (uint32_t y = 0; y < h; ++y) {
      png_read_row(png, row.data(), nullptr);
      for (uint32_t x = 0; x < w; ++x) {
        uint8_t R = row[x * 4 + 0], G = row[x * 4 + 1], B = row[x * 4 + 2], A = row[x * 4 + 3];
        px[static_cast<size_t>(y) * w + x] =
          (uint32_t(A) << 24) | (uint32_t(R) << 16) | (uint32_t(G) << 8) | B;
      }
    }
    png_read_end(png, nullptr);
    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(fp);
    return true;
  }

  static std::string sibling(const std::string &golden, const std::string &suffix) {
    std::string base = golden;
    if (base.size() > 4 && base.substr(base.size() - 4) == ".png")
      base = base.substr(0, base.size() - 4);
    return base + suffix;
  }

  static void mkdirs(const std::string &path) {
    std::string acc;
    for (size_t i = 0; i <= path.size(); ++i) {
      if (i == path.size() || path[i] == '/') {
        if (!acc.empty() && acc != ".") mkdir(acc.c_str(), 0755);
      }
      if (i < path.size()) acc += path[i];
    }
  }

  bool compareGolden(const Frame &f, const std::string &golden_path,
                     int tolerance, int pixel_budget) {
    const char *bless = std::getenv("BLESS");
    if (bless && bless[0] && std::strcmp(bless, "0") != 0) {
      size_t slash = golden_path.find_last_of('/');
      if (slash != std::string::npos) mkdirs(golden_path.substr(0, slash));
      if (!writePNG(golden_path, f.w, f.h, f.pixels)) {
        std::fprintf(stderr, "BLESS: failed to write golden %s\n", golden_path.c_str());
        return false;
      }
      std::fprintf(stderr, "BLESS: wrote golden %s (%ux%u)\n",
                   golden_path.c_str(), f.w, f.h);
      return true;
    }

    uint32_t gw = 0, gh = 0;
    std::vector<uint32_t> gp;
    if (!readPNG(golden_path, gw, gh, gp)) {
      std::fprintf(stderr,
        "compareGolden: cannot read golden %s (run with BLESS=1 to create it)\n",
        golden_path.c_str());
      return false;
    }
    if (gw != f.w || gh != f.h) {
      std::fprintf(stderr, "compareGolden: dimension mismatch golden %ux%u vs frame %ux%u\n",
                   gw, gh, f.w, f.h);
      return false;
    }

    int violations = 0;
    std::vector<uint32_t> diff(static_cast<size_t>(f.w) * f.h, 0xFF000000u);
    for (size_t i = 0; i < f.pixels.size(); ++i) {
      uint32_t a = f.pixels[i], b = gp[i];
      int dr = std::abs(int((a >> 16) & 0xFF) - int((b >> 16) & 0xFF));
      int dg = std::abs(int((a >> 8) & 0xFF) - int((b >> 8) & 0xFF));
      int db = std::abs(int(a & 0xFF) - int(b & 0xFF));
      if (std::max(dr, std::max(dg, db)) > tolerance) {
        ++violations;
        diff[i] = 0xFFFF0000u; // red marks a differing pixel
      }
    }
    if (violations > pixel_budget) {
      std::fprintf(stderr, "compareGolden: %d pixels exceed tolerance %d (budget %d)\n",
                   violations, tolerance, pixel_budget);
      writePNG(sibling(golden_path, "-actual.png"), f.w, f.h, f.pixels);
      writePNG(sibling(golden_path, "-diff.png"), f.w, f.h, diff);
      return false;
    }
    return true;
  }

} // namespace bbai::test
