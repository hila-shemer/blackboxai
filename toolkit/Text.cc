#include "Text.hh"
#include "text.hpp"  // the fcft + pixman C-interop boundary

#include <string>

namespace bt {

  namespace {
    // fcft_init/fcft_fini are process-global; refcount so multiple renderers and
    // the unit boundary test can coexist. Single-threaded (compositor + tests).
    int g_fcft_refs = 0;
    void fcftRef() {
      if (g_fcft_refs++ == 0)
        fcft_init(FCFT_LOG_COLORIZE_NEVER, /*do_syslog=*/false, FCFT_LOG_CLASS_ERROR);
    }
    void fcftUnref() {
      if (--g_fcft_refs == 0)
        fcft_fini();
    }

    // Blend foreground over an opaque background by coverage `a` (0..255),
    // round-to-nearest. The +127 must match in tests for byte-exact goldens.
    inline uint8_t blend8(uint8_t fg, uint8_t bg, uint8_t a) {
      return static_cast<uint8_t>((fg * a + bg * (255 - a) + 127) / 255);
    }
  }

  TextRenderer::TextRenderer(const std::string &family, int pixelsize) {
    fcftRef();
    const char *names[1] = { family.c_str() };
    const std::string attrs = "pixelsize=" + std::to_string(pixelsize);
    font_ = fcft_from_name(1, names, attrs.c_str());
  }

  TextRenderer::~TextRenderer() {
    if (font_) fcft_destroy(font_);
    fcftUnref();
  }

  int TextRenderer::ascent() const  { return font_ ? font_->ascent  : 0; }
  int TextRenderer::descent() const { return font_ ? font_->descent : 0; }
  int TextRenderer::height() const  { return font_ ? font_->height  : 0; }

  int TextRenderer::textWidth(std::u32string_view text) const {
    if (!font_) return 0;
    int w = 0;
    for (char32_t cp : text) {
      const fcft_glyph *g = fcft_rasterize_char_utf32(font_, cp, FCFT_SUBPIXEL_NONE);
      if (g) w += g->advance.x;
    }
    return w;
  }

  int TextRenderer::drawText(std::vector<uint32_t> &argb, int bufW, int bufH,
                             int penX, int baselineY, std::u32string_view text,
                             const Color &color) const {
    if (!font_) return penX;
    const uint8_t tr = static_cast<uint8_t>(color.red());
    const uint8_t tg = static_cast<uint8_t>(color.green());
    const uint8_t tb = static_cast<uint8_t>(color.blue());

    for (char32_t cp : text) {
      const fcft_glyph *g = fcft_rasterize_char_utf32(font_, cp, FCFT_SUBPIXEL_NONE);
      if (!g) continue;
      const int gx0 = penX + g->x;          // glyph box left  (bearing x)
      const int gy0 = baselineY - g->y;     // glyph box top   (baseline - bearing y)
      const int stride = pixman_image_get_stride(g->pix);
      const uint8_t *data =
        reinterpret_cast<const uint8_t *>(pixman_image_get_data(g->pix));

      if (g->is_color_glyph) {
        // Color (emoji/CBDT) glyphs: pix is premultiplied a8r8g8b8, blended
        // src-over directly (no text color). The bundled monospace font has no
        // color glyphs, so M3 titles never reach this path and it is unverified
        // by tests; a window title containing an emoji would exercise it.
        const int pxstride = stride / 4;
        const uint32_t *px = reinterpret_cast<const uint32_t *>(data);
        for (int yy = 0; yy < g->height; ++yy) {
          const int dy = gy0 + yy;
          if (dy < 0 || dy >= bufH) continue;
          for (int xx = 0; xx < g->width; ++xx) {
            const int dx = gx0 + xx;
            if (dx < 0 || dx >= bufW) continue;
            const uint32_t s = px[yy * pxstride + xx];
            const uint8_t sa = s >> 24, sr = (s >> 16) & 0xFF,
                          sg = (s >> 8) & 0xFF, sb = s & 0xFF;
            uint32_t &d = argb[static_cast<size_t>(dy) * bufW + dx];
            const uint8_t br = (d >> 16) & 0xFF, bg = (d >> 8) & 0xFF, bb = d & 0xFF;
            const uint8_t r  = static_cast<uint8_t>(sr + (br * (255 - sa) + 127) / 255);
            const uint8_t gg = static_cast<uint8_t>(sg + (bg * (255 - sa) + 127) / 255);
            const uint8_t bl = static_cast<uint8_t>(sb + (bb * (255 - sa) + 127) / 255);
            d = 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(gg) << 8) | bl;
          }
        }
      } else {
        // a8 coverage mask painted in the text color.
        for (int yy = 0; yy < g->height; ++yy) {
          const int dy = gy0 + yy;
          if (dy < 0 || dy >= bufH) continue;
          for (int xx = 0; xx < g->width; ++xx) {
            const int dx = gx0 + xx;
            if (dx < 0 || dx >= bufW) continue;
            const uint8_t a = data[yy * stride + xx];
            if (!a) continue;
            uint32_t &d = argb[static_cast<size_t>(dy) * bufW + dx];
            const uint8_t br = (d >> 16) & 0xFF, bg = (d >> 8) & 0xFF, bb = d & 0xFF;
            d = 0xFF000000u
              | (uint32_t(blend8(tr, br, a)) << 16)
              | (uint32_t(blend8(tg, bg, a)) << 8)
              |  uint32_t(blend8(tb, bb, a));
          }
        }
      }
      penX += g->advance.x;
    }
    return penX;
  }

} // namespace bt
