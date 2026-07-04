#include "Toolbar.hh"
#include "Server.hh"
#include "Output.hh"
#include "Workspace.hh"
#include "DataBuffer.hh"
#include "Style.hh"
#include "Render.hh"

#include "Texture.hh"
#include "Image.hh"
#include "Text.hh"
#include "Clock.hh"

#include <algorithm>
#include <memory>

namespace bbai {

  namespace {
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

  Toolbar::Toolbar(Server &server, Output &output)
    : server_(server), output_(output),
      ow_(output.wlrOutput()->width), oh_(output.wlrOutput()->height) {
    tree_ = wlr_scene_tree_create(server_.layer_top);
    rebuild();
    output_.addStrut(&strut_);
    updateStrut();
    // Per-minute clock tick (deterministic in tests via the VirtualClock).
    clock_timer_ = std::make_unique<Timer>(server_.timerRegistry(), *this);
    clock_timer_->start(60000, /*recurring=*/true);
    hide_timer_ = std::make_unique<Timer>(server_.timerRegistry(), hide_handler_);
  }

  Toolbar::~Toolbar() {
    output_.removeStrut(&strut_);
    hide_timer_.reset();    // unregister before the registry
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

  toolbar::Rect Toolbar::currentBarRect() const {
    return toolbar::barRect(ow_, oh_, placement_,
                            server_.currentStyle()->toolbarMetrics(),
                            server_.config().toolbar.widthPercent);
  }

  bool Toolbar::containsGlobal(int gx, int gy) const {
    const toolbar::Rect b = currentBarRect();
    return gx >= b.x && gx < b.x + b.w && gy >= b.y && gy < b.y + b.h;
  }

  void Toolbar::rebuild(void) {
    clearNodes();
    std::shared_ptr<const Style> st = server_.currentStyle();
    const ToolbarLook &look = st->toolbarLook();
    const toolbar::ToolbarMetrics m = st->toolbarMetrics();
    const toolbar::Rect bar = currentBarRect();
    bar_rect_ = bar;

    bt::TextRenderer *font = st->toolbarFont();
    const std::string ws_name = server_.workspaces().name(server_.workspaces().current());
    const std::u32string ws_u32 = bt::decodeUtf8(ws_name.c_str());
    const std::u32string clk_u32 = bt::decodeUtf8(clockText().c_str());

    // Workspace-label and clock widths are equalized to the wider text.
    const int max_text = std::max(font->textWidth(ws_u32), font->textWidth(clk_u32));
    label_w_ = clock_w_ = toolbar::labelWidth(max_text, m);
    sections_ = toolbar::sectionRects(bar.w, label_w_, clock_w_, m);

    const int baseline =
      std::max(0, (m.labelHeight - font->height()) / 2) + font->ascent();

    // Bar base (lowest); keep its pixels for parentrelative sections.
    bar_px_ = bt::Image(bar.w, bar.h).renderBuffer(look.bar);
    emit({0, 0, bar.w, bar.h}, std::vector<uint32_t>(bar_px_));

    auto sectionPx = [&](const bt::Texture &tex, toolbar::Rect r) {
      if (tex.texture() == bt::Texture::Parent_Relative)
        return render::cropOrBlack(bar_px_, bar.w, bar.h, r.x, r.y, r.w, r.h);
      return bt::Image(r.w, r.h).renderBuffer(tex);
    };
    auto label = [&](toolbar::Rect r, const bt::Texture &tex, const bt::Color &tc,
                     const std::u32string &text) {
      std::vector<uint32_t> px = sectionPx(tex, r);
      if (font->ok() && !text.empty())
        font->drawText(px, r.w, r.h, /*penX=*/1, baseline, text, tc);
      emit(r, std::move(px));
    };
    auto button = [&](toolbar::Rect r, bool right) {
      std::vector<uint32_t> px = sectionPx(look.button, r);
      drawArrow(px, m.buttonWidth, right, look.foreground);
      emit(r, std::move(px));
    };

    label(sections_.workspace_label, look.slabel, look.slabelText, ws_u32);
    button(sections_.prev_ws, /*right=*/false);
    button(sections_.next_ws, /*right=*/true);
    label(sections_.window_label, look.wlabel, look.wlabelText,
          bt::decodeUtf8(window_title_.c_str()));
    button(sections_.prev_win, /*right=*/false);
    button(sections_.next_win, /*right=*/true);
    redrawClock();
    applyPosition();
  }

  void Toolbar::redrawClock(void) {
    if (clock_node_) { wlr_scene_node_destroy(&clock_node_->node); clock_node_ = nullptr; }
    std::shared_ptr<const Style> st = server_.currentStyle();
    const ToolbarLook &look = st->toolbarLook();
    const toolbar::ToolbarMetrics m = st->toolbarMetrics();
    bt::TextRenderer *font = st->toolbarFont();
    std::vector<uint32_t> px =
      (look.clock.texture() == bt::Texture::Parent_Relative)
        ? render::cropOrBlack(bar_px_, bar_rect_.w, bar_rect_.h,
                              sections_.clock.x, sections_.clock.y,
                              sections_.clock.w, sections_.clock.h)
        : bt::Image(clock_w_, m.labelHeight).renderBuffer(look.clock);
    if (font->ok()) {
      const int baseline =
        std::max(0, (m.labelHeight - font->height()) / 2) + font->ascent();
      font->drawText(px, clock_w_, m.labelHeight, /*penX=*/1, baseline,
                     bt::decodeUtf8(clockText().c_str()), look.clockText);
    }
    emit(sections_.clock, std::move(px), /*is_clock=*/true);
  }

  void Toolbar::applyPosition(void) {
    const toolbar::Rect shown = currentBarRect();
    const toolbar::Rect r = hidden_
      ? toolbar::hiddenBarRect(shown, placement_, server_.currentStyle()->toolbarMetrics())
      : shown;
    wlr_scene_node_set_position(&tree_->node, r.x, r.y);
  }

  void Toolbar::onHideTimeout(void) {
    hidden_ = !hidden_;
    applyPosition();
  }

  void Toolbar::handlePointerMotion(double x, double y) {
    const toolbar::Rect b = currentBarRect();   // shown footprint = hot zone
    const bool over = (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h);
    onPointerOverToolbar(over);   // no-op when auto_hide_ is off
  }

  void Toolbar::onPointerOverToolbar(bool over) {
    if (!auto_hide_) return;
    const bool want_shown = over;             // pointer over the bar/sliver -> reveal
    if (want_shown == !hidden_) { hide_timer_->stop(); return; }   // already in/heading to the right state
    hide_timer_->start(kHideDelayMs, /*recurring=*/false);         // one-shot toward the toggle
  }

  void Toolbar::setPlacement(toolbar::Placement p) {
    placement_ = p;
    rebuild();
    updateStrut();
  }

  void Toolbar::setAutoHide(bool on) {
    auto_hide_ = on;
    hidden_ = on;
    applyPosition();
    updateStrut();
  }

  // The strut derives from the LIVE bar rect (style metrics + config width),
  // the same one rebuild/applyPosition draw - never the constexpr defaults,
  // or a style-sized bar overlaps maximized windows. Auto-hide reserves the
  // style's sliver (classic 2px under default metrics), not zero: a maximized
  // window must not cover the reveal trigger.
  void Toolbar::updateStrut(void) {
    const toolbar::Rect bar = currentBarRect();
    const bool top = (placement_ == toolbar::Placement::TopLeft ||
                      placement_ == toolbar::Placement::TopCenter ||
                      placement_ == toolbar::Placement::TopRight);
    const int exposed = auto_hide_
      ? server_.currentStyle()->toolbarMetrics().hiddenHeight : bar.h;
    strut_ = Strut{};
    if (top) strut_.top = exposed;
    else     strut_.bottom = exposed;
    output_.strutsChanged();
  }

} // namespace bbai
