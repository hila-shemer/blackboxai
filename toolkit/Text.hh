// bt::TextRenderer — rasterizes UTF-32 text via fcft and blends it into a
// premultiplied ARGB8888 buffer (the same buffer model bt::Image::renderBuffer
// produces, so titlebar labels compose by drawing text over a rendered texture
// buffer before it is handed to DataBuffer::create).
//
// The fcft/pixman includes live only in Text.cc (via toolkit/text.hpp); this
// header exposes fcft_font as an opaque pointer so includers don't pull the
// font stack.
#ifndef BLACKBOXAI_TEXT_HH
#define BLACKBOXAI_TEXT_HH

#include "Color.hh"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct fcft_font;

namespace bt {

  class TextRenderer {
  public:
    // `family` is a fontconfig family name (resolved under the active
    // FONTCONFIG_FILE); `pixelsize` is an exact pixel size (DPI-independent, so
    // the raster is deterministic). ok() is false if the font failed to load.
    TextRenderer(const std::string &family, int pixelsize);
    ~TextRenderer();
    TextRenderer(const TextRenderer &) = delete;
    TextRenderer &operator=(const TextRenderer &) = delete;

    bool ok() const { return font_ != nullptr; }

    int ascent() const;   // baseline offset from the top of the line
    int descent() const;
    int height() const;   // line spacing
    int textWidth(std::u32string_view text) const;  // sum of glyph advances

    // Blend `text` in `color` into `argb` (row-major premultiplied ARGB8888,
    // bufW x bufH), pen starting at penX with the baseline at baselineY. Glyphs
    // are clipped to the buffer. Returns the pen x after the last glyph.
    int drawText(std::vector<uint32_t> &argb, int bufW, int bufH,
                 int penX, int baselineY, std::u32string_view text,
                 const Color &color) const;

  private:
    fcft_font *font_ = nullptr;
  };

} // namespace bt

#endif // BLACKBOXAI_TEXT_HH
