#include "Decoration.hh"
#include "DataBuffer.hh"
#include "Style.hh"
#include "Render.hh"

#include "Texture.hh"
#include "Image.hh"
#include "Text.hh"

#include <cstdint>

namespace bbai {

  namespace {
    inline void setPx(std::vector<uint32_t> &px, int w, int h, int x, int y,
                      const bt::Color &c) {
      if (x < 0 || y < 0 || x >= w || y >= h) return;
      px[size_t(y) * w + x] = 0xFF000000u | (uint32_t(c.red()) << 16)
                            | (uint32_t(c.green()) << 8) | uint32_t(c.blue());
    }

    // Button glyphs, drawn into a `bw`x`bw` button buffer in pic color.
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

  Decoration::Decoration(wlr_scene_tree *p) : parent(p) {}
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

  void Decoration::rebuild(const Style &st, int W, int H,
                           const char *titleText, bool focused) {
    clear();
    using namespace frame;
    const WindowLook &look = st.windowLook(focused);
    const FrameMetrics &m = st.frameMetrics();
    bt::TextRenderer *font = st.windowFont();

    // Side borders first (lowest), then textured elements over them.
    emitRect(leftBorder(W, H, m),  look.frameBorder);
    emitRect(rightBorder(W, H, m), look.frameBorder);

    // Titlebar. Its buffer is KEPT for parent-relative children below.
    const Rect tr = title(W, H, m);
    std::vector<uint32_t> title_px =
      bt::Image(tr.w, tr.h).renderBuffer(look.title);
    emit(tr, std::vector<uint32_t>(title_px));   // copy in; original stays for crops

    // An element's pixels: its own texture, or - parentrelative - a crop of the
    // titlebar it sits on (the classic behavior Gray's labels/buttons rely on).
    auto elementPx = [&](const bt::Texture &tex, Rect r) {
      if (tex.texture() == bt::Texture::Parent_Relative)
        return render::cropOrBlack(title_px, tr.w, tr.h, r.x - tr.x, r.y - tr.y, r.w, r.h);
      return bt::Image(r.w, r.h).renderBuffer(tex);
    };

    // Label with the window title text, left-aligned, vertically centred.
    {
      const Rect lr = label(W, H, m);
      std::vector<uint32_t> px = elementPx(look.label, lr);
      if (font && font->ok()) {
        const int top_pad = (m.labelHeight - font->height()) / 2;
        const int baseline = (top_pad > 0 ? top_pad : 0) + font->ascent();
        font->drawText(px, lr.w, lr.h, /*penX=*/1, baseline, bt::decodeUtf8(titleText),
                       look.text);
      }
      emit(lr, std::move(px));
    }

    // Buttons: iconify | ... | maximize close.
    auto button = [&](Rect r, void (*glyph)(std::vector<uint32_t> &, int, const bt::Color &)) {
      std::vector<uint32_t> px = elementPx(look.button, r);
      glyph(px, m.buttonWidth, look.foreground);
      emit(r, std::move(px));
    };
    button(iconifyButton(W, H, m),  drawIconifyGlyph);
    button(maximizeButton(W, H, m), drawMaximizeGlyph);
    button(closeButton(W, H, m),    drawCloseGlyph);

    // Handle and the two resize grips on top of it. (PR handle/grip textures
    // were force-flattened by the Style loader's sanity rules.)
    emit(handle(W, H, m), bt::Image(frameWidth(W, m), m.handleHeight).renderBuffer(look.handle));
    emit(leftGrip(W, H, m),  bt::Image(m.gripWidth, m.handleHeight).renderBuffer(look.grip));
    emit(rightGrip(W, H, m), bt::Image(m.gripWidth, m.handleHeight).renderBuffer(look.grip));
  }

} // namespace bbai
