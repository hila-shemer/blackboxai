#include "Decoration.hh"
#include "DataBuffer.hh"

#include "Texture.hh"
#include "Image.hh"
#include "Text.hh"

#include <cstdint>

namespace bbai {

  namespace {
    // Default M3 frame palette (style fidelity is M5; these are self-contained
    // defaults using colors the toolkit parser knows or plain hex). Classic
    // Blackbox grey, focused brighter than unfocused.
    struct Look { const char *desc; const char *c1; const char *c2; };

    Look titleLook (bool f) { return f ? Look{"raised gradient diagonal", "#c0c0c0", "#808080"}
                                       : Look{"flat solid",                "#808080", ""}; }
    Look labelLook (bool f) { return f ? Look{"sunken gradient diagonal", "#b8b8b8", "#888888"}
                                       : Look{"flat solid",               "#707070", ""}; }
    Look handleLook(bool f) { return f ? Look{"raised gradient diagonal", "#c0c0c0", "#808080"}
                                       : Look{"flat solid",               "#707070", ""}; }
    Look gripLook  (bool f) { return f ? Look{"raised gradient diagonal", "#d8d8d8", "#909090"}
                                       : Look{"raised solid",             "#909090", ""}; }
    Look buttonLook(bool f) { return f ? Look{"raised gradient diagonal", "#e0e0e0", "#a8a8a8"}
                                       : Look{"flat solid",               "#989898", ""}; }

    bt::Color textColor(bool f)   { return f ? bt::Color(0, 0, 0)    : bt::Color(56, 56, 56); }
    bt::Color borderColor()       { return bt::Color(48, 48, 48); }
    bt::Color picColor()          { return bt::Color(32, 32, 32); }

    bt::Texture makeTexture(const Look &l) {
      bt::Texture t;
      t.setDescription(l.desc);
      t.setColor1(bt::Color::fromString(l.c1));
      if (l.c2 && *l.c2) t.setColor2(bt::Color::fromString(l.c2));
      return t;
    }

    std::vector<uint32_t> renderTexture(int w, int h, const Look &l) {
      bt::Image img(w, h);
      return img.renderBuffer(makeTexture(l));
    }

    // Minimal UTF-8 -> UTF-32 (enough for window titles; malformed bytes pass
    // through as their raw value so rendering never throws).
    std::u32string decodeUtf8(const char *s) {
      std::u32string out;
      if (!s) return out;
      const auto *p = reinterpret_cast<const unsigned char *>(s);
      while (*p) {
        char32_t cp; int extra;
        if (*p < 0x80)        { cp = *p;        extra = 0; }
        else if (*p < 0xE0)   { cp = *p & 0x1F; extra = 1; }
        else if (*p < 0xF0)   { cp = *p & 0x0F; extra = 2; }
        else                  { cp = *p & 0x07; extra = 3; }
        ++p;
        for (int i = 0; i < extra && (*p & 0xC0) == 0x80; ++i, ++p)
          cp = (cp << 6) | (*p & 0x3F);
        out.push_back(cp);
      }
      return out;
    }

    inline void setPx(std::vector<uint32_t> &px, int w, int h, int x, int y,
                      const bt::Color &c) {
      if (x < 0 || y < 0 || x >= w || y >= h) return;
      px[size_t(y) * w + x] = 0xFF000000u | (uint32_t(c.red()) << 16)
                            | (uint32_t(c.green()) << 8) | uint32_t(c.blue());
    }

    // Button glyphs, drawn into a `bw`x`bw` button buffer in pic color. `m` is
    // the inset margin; the mark sits in the box [m, bw-m).
    void drawCloseGlyph(std::vector<uint32_t> &px, int bw, const bt::Color &c) {
      const int m = 5, hi = bw - m - 1;
      for (int i = 0; i <= hi - m; ++i) {
        setPx(px, bw, bw, m + i, m + i, c);        // '\' diagonal
        setPx(px, bw, bw, hi - i, m + i, c);       // '/' diagonal
      }
    }
    void drawMaximizeGlyph(std::vector<uint32_t> &px, int bw, const bt::Color &c) {
      const int m = 4, hi = bw - m - 1;
      for (int x = m; x <= hi; ++x) { setPx(px, bw, bw, x, m, c); setPx(px, bw, bw, x, m + 1, c); setPx(px, bw, bw, x, hi, c); }
      for (int y = m; y <= hi; ++y) { setPx(px, bw, bw, m, y, c); setPx(px, bw, bw, hi, y, c); }
    }
    void drawIconifyGlyph(std::vector<uint32_t> &px, int bw, const bt::Color &c) {
      const int m = 5, hi = bw - m - 1, y = bw - m - 1;
      for (int x = m; x <= hi; ++x) { setPx(px, bw, bw, x, y, c); setPx(px, bw, bw, x, y - 1, c); }
    }
  } // namespace

  Decoration::Decoration(wlr_scene_tree *p, bt::TextRenderer *f) : parent(p), font(f) {}
  Decoration::~Decoration() { clear(); }

  void Decoration::clear() {
    for (wlr_scene_node *n : nodes) wlr_scene_node_destroy(n);
    nodes.clear();
  }

  void Decoration::emit(frame::Rect r, std::vector<uint32_t> pixels) {
    DataBuffer *buf = DataBuffer::create(r.w, r.h, std::move(pixels));
    wlr_scene_buffer *sb = wlr_scene_buffer_create(parent, buf->base());
    wlr_buffer_drop(buf->base());  // scene_buffer took its own ref
    wlr_scene_node_set_position(&sb->node, r.x, r.y);
    nodes.push_back(&sb->node);
  }

  void Decoration::emitRect(frame::Rect r, const bt::Color &c) {
    const float color[4] = { c.red() / 255.0f, c.green() / 255.0f, c.blue() / 255.0f, 1.0f };
    wlr_scene_rect *rect = wlr_scene_rect_create(parent, r.w, r.h, color);
    wlr_scene_node_set_position(&rect->node, r.x, r.y);
    nodes.push_back(&rect->node);
  }

  void Decoration::rebuild(int W, int H, bool focused, const char *titleText) {
    clear();
    using namespace frame;

    // Side borders first (lowest), then textured elements over them.
    emitRect(leftBorder(W, H),  borderColor());
    emitRect(rightBorder(W, H), borderColor());

    // Titlebar.
    emit(title(W, H), renderTexture(frameWidth(W), kTitleHeight, titleLook(focused)));

    // Label with the window title text, left-aligned, vertically centred.
    {
      const Rect lr = label(W, H);
      std::vector<uint32_t> px = renderTexture(lr.w, lr.h, labelLook(focused));
      if (font && font->ok()) {
        const int top_pad = (kLabelHeight - font->height()) / 2;
        const int baseline = (top_pad > 0 ? top_pad : 0) + font->ascent();
        font->drawText(px, lr.w, lr.h, /*penX=*/1, baseline, decodeUtf8(titleText),
                       textColor(focused));
      }
      emit(lr, std::move(px));
    }

    // Buttons (drawn; only close is wired, later). iconify | ... | maximize close
    auto button = [&](Rect r, void (*glyph)(std::vector<uint32_t> &, int, const bt::Color &)) {
      std::vector<uint32_t> px = renderTexture(r.w, r.h, buttonLook(focused));
      glyph(px, kButtonWidth, picColor());
      emit(r, std::move(px));
    };
    button(iconifyButton(W, H),  drawIconifyGlyph);
    button(maximizeButton(W, H), drawMaximizeGlyph);
    button(closeButton(W, H),    drawCloseGlyph);

    // Handle and the two resize grips on top of it.
    emit(handle(W, H), renderTexture(frameWidth(W), kHandleHeight, handleLook(focused)));
    emit(leftGrip(W, H),  renderTexture(kGripWidth, kHandleHeight, gripLook(focused)));
    emit(rightGrip(W, H), renderTexture(kGripWidth, kHandleHeight, gripLook(focused)));
  }

} // namespace bbai
