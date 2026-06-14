#include "Toolbar.hh"
#include "Server.hh"
#include "Workspace.hh"
#include "DataBuffer.hh"

#include "Texture.hh"
#include "Image.hh"
#include "Text.hh"
#include "Clock.hh"

#include <algorithm>

namespace bbai {

  namespace {
    using toolbar::kLabelHeight;
    using toolbar::kButtonWidth;

    struct Look { const char *desc; const char *c1; const char *c2; };
    // Grey palette matching the M3 decoration family (self-contained M4 default).
    constexpr Look kBarLook   {"raised gradient diagonal", "#c0c0c0", "#808080"};
    constexpr Look kLabelLook {"sunken gradient diagonal", "#b8b8b8", "#888888"};
    constexpr Look kButtonLook{"raised gradient diagonal", "#e0e0e0", "#a8a8a8"};

    bt::Color textColor() { return bt::Color(0, 0, 0); }
    bt::Color picColor()  { return bt::Color(32, 32, 32); }

    bt::Texture makeTexture(const Look &l) {
      bt::Texture t;
      t.setDescription(l.desc);
      t.setColor1(bt::Color::fromString(l.c1));
      if (l.c2 && *l.c2) t.setColor2(bt::Color::fromString(l.c2));
      return t;
    }
    std::vector<uint32_t> render(int w, int h, const Look &l) {
      return bt::Image(w, h).renderBuffer(makeTexture(l));
    }
    void setPx(std::vector<uint32_t> &px, int w, int h, int x, int y, const bt::Color &c) {
      if (x < 0 || y < 0 || x >= w || y >= h) return;
      px[size_t(y) * w + x] = 0xFF000000u | (uint32_t(c.red()) << 16)
                            | (uint32_t(c.green()) << 8) | uint32_t(c.blue());
    }
    // A filled 9px arrow triangle centred in a `bw`-wide button.
    void drawArrow(std::vector<uint32_t> &px, int bw, bool right, const bt::Color &c) {
      const int cx = bw / 2, cy = bw / 2;
      for (int d = 0; d <= 8; ++d) {
        const int x = right ? cx - 4 + d : cx + 4 - d;
        const int vh = (8 - d) / 2;            // tall at the base, a point at the tip
        for (int dy = -vh; dy <= vh; ++dy) setPx(px, bw, bw, x, cy + dy, c);
      }
    }
  } // namespace

  Toolbar::Toolbar(Server &server, int output_w, int output_h)
    : server_(server), ow_(output_w), oh_(output_h) {
    tree_ = wlr_scene_tree_create(server_.layer_top);
    rebuild();
    // Per-minute clock tick (deterministic in tests via the VirtualClock).
    clock_timer_ = std::make_unique<Timer>(server_.timerRegistry(), *this);
    clock_timer_->start(60000, /*recurring=*/true);
  }

  Toolbar::~Toolbar() {
    clock_timer_.reset();   // unregister before the registry
    clearNodes();
    if (tree_) wlr_scene_node_destroy(&tree_->node);
  }

  void Toolbar::timeout(void) { redrawClock(); }

  void Toolbar::clearNodes(void) {
    for (wlr_scene_node *n : nodes_) wlr_scene_node_destroy(n);
    nodes_.clear();
    if (clock_node_) { wlr_scene_node_destroy(&clock_node_->node); clock_node_ = nullptr; }
  }

  void Toolbar::emit(toolbar::Rect r, std::vector<uint32_t> px, bool is_clock) {
    DataBuffer *buf = DataBuffer::create(r.w, r.h, std::move(px));
    wlr_scene_buffer *sb = wlr_scene_buffer_create(tree_, buf->base());
    wlr_buffer_drop(buf->base());
    wlr_scene_node_set_position(&sb->node, r.x, r.y);
    if (is_clock) clock_node_ = sb;
    else nodes_.push_back(&sb->node);
  }

  std::string Toolbar::clockText(void) const {
    return bt::formatClock(server_.clock().wallSeconds());
  }

  void Toolbar::redrawWorkspaceLabel(void) { rebuild(); }

  void Toolbar::redrawWindowLabel(const char *title) {
    window_title_ = title ? title : "";
    rebuild();
  }

  void Toolbar::rebuild(void) {
    clearNodes();
    const toolbar::Rect bar = toolbar::barRect(ow_, oh_);
    wlr_scene_node_set_position(&tree_->node, bar.x, bar.y);

    bt::TextRenderer *font = server_.titleFont();
    const std::string ws_name = server_.workspaces().name(server_.workspaces().current());
    const std::u32string ws_u32 = bt::decodeUtf8(ws_name.c_str());
    const std::u32string clk_u32 = bt::decodeUtf8(clockText().c_str());

    // Workspace-label and clock widths are equalized to the wider text.
    const int max_text = std::max(font->textWidth(ws_u32), font->textWidth(clk_u32));
    label_w_ = clock_w_ = toolbar::labelWidth(max_text);
    sections_ = toolbar::sectionRects(bar.w, label_w_, clock_w_);

    const int baseline =
      std::max(0, (kLabelHeight - font->height()) / 2) + font->ascent();

    // Bar base (lowest), then the sections over it (z-order like Decoration).
    emit({0, 0, bar.w, bar.h}, render(bar.w, bar.h, kBarLook));

    auto label = [&](toolbar::Rect r, const std::u32string &text) {
      std::vector<uint32_t> px = render(r.w, r.h, kLabelLook);
      if (font->ok() && !text.empty())
        font->drawText(px, r.w, r.h, /*penX=*/1, baseline, text, textColor());
      emit(r, std::move(px));
    };
    auto button = [&](toolbar::Rect r, bool right) {
      std::vector<uint32_t> px = render(r.w, r.h, kButtonLook);
      drawArrow(px, kButtonWidth, right, picColor());
      emit(r, std::move(px));
    };

    label(sections_.workspace_label, ws_u32);
    button(sections_.prev_ws, /*right=*/false);
    button(sections_.next_ws, /*right=*/true);
    label(sections_.window_label, bt::decodeUtf8(window_title_.c_str()));
    button(sections_.prev_win, /*right=*/false);
    button(sections_.next_win, /*right=*/true);
    redrawClock();
  }

  void Toolbar::redrawClock(void) {
    if (clock_node_) { wlr_scene_node_destroy(&clock_node_->node); clock_node_ = nullptr; }
    bt::TextRenderer *font = server_.titleFont();
    std::vector<uint32_t> px = render(clock_w_, kLabelHeight, kLabelLook);
    if (font->ok()) {
      const int baseline =
        std::max(0, (kLabelHeight - font->height()) / 2) + font->ascent();
      font->drawText(px, clock_w_, kLabelHeight, /*penX=*/1, baseline,
                     bt::decodeUtf8(clockText().c_str()), textColor());
    }
    emit(sections_.clock, std::move(px), /*is_clock=*/true);
  }

} // namespace bbai
