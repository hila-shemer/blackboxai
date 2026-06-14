#include "Menu.hh"
#include "Server.hh"
#include "DataBuffer.hh"

#include "Texture.hh"
#include "Image.hh"
#include "Text.hh"

#include <algorithm>

namespace bbai {

  namespace {
    struct Look { const char *desc; const char *c1; const char *c2; };
    constexpr Look kFrameLook {"raised gradient diagonal", "#c8c8c8", "#a8a8a8"};
    constexpr Look kTitleLook {"raised gradient diagonal", "#b0b0b0", "#888888"};
    constexpr Look kHiliteLook{"raised gradient diagonal", "#5a7abf", "#33558f"};

    bt::Color frameText()    { return bt::Color(0, 0, 0); }
    bt::Color activeText()   { return bt::Color(255, 255, 255); }
    bt::Color disabledText() { return bt::Color(112, 112, 112); }
    bt::Color sepColor()     { return bt::Color(96, 96, 96); }

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
    // A small check mark in the left gutter (for the current-workspace ✓).
    void drawCheck(std::vector<uint32_t> &px, int w, int h, const bt::Color &c) {
      const int cy = h / 2, x0 = 4;
      for (int i = 0; i < 3; ++i) setPx(px, w, h, x0 + i, cy + i, c);
      for (int i = 0; i < 5; ++i) setPx(px, w, h, x0 + 2 + i, cy + 2 - i, c);
    }
  } // namespace

  Menu::Menu(Server &server, std::u32string title, std::vector<MenuItem> items)
    : server_(server), title_(std::move(title)), items_(std::move(items)) {
    tree_ = wlr_scene_tree_create(server_.layer_overlay);
    bt::TextRenderer *font = server_.titleFont();
    metrics_.reserve(items_.size());
    for (const MenuItem &it : items_)
      metrics_.push_back({ it.separator() ? 0 : font->textWidth(it.label), it.separator() });
    layout_ = menu::computeLayout(metrics_, font->height(), /*show_title=*/true,
                                  font->textWidth(title_), font->height());
    item_nodes_.assign(items_.size(), nullptr);
  }

  Menu::~Menu() {
    if (tree_) wlr_scene_node_destroy(&tree_->node);  // destroys all children
  }

  void Menu::show(int gx, int gy) {
    int ow = 1280, oh = 720;
    server_.activeOutputSize(ow, oh);
    // Clamp so the whole menu stays on-screen.
    if (gx + layout_.width > ow) gx = ow - layout_.width;
    if (gy + layout_.height > oh) gy = oh - layout_.height;
    if (gx < 0) gx = 0;
    if (gy < 0) gy = 0;
    gx_ = gx; gy_ = gy;
    wlr_scene_node_set_position(&tree_->node, gx_, gy_);

    bt::TextRenderer *font = server_.titleFont();
    const int baseline = std::max(0, (layout_.title_h - font->height()) / 2) + font->ascent();

    // Frame background (lowest).
    {
      std::vector<uint32_t> px = render(layout_.width, layout_.height, kFrameLook);
      DataBuffer *buf = DataBuffer::create(layout_.width, layout_.height, std::move(px));
      wlr_scene_buffer *sb = wlr_scene_buffer_create(tree_, buf->base());
      wlr_buffer_drop(buf->base());
      wlr_scene_node_set_position(&sb->node, 0, 0);
      frame_node_ = &sb->node;
    }
    // Title bar.
    {
      std::vector<uint32_t> px = render(layout_.width, layout_.title_h, kTitleLook);
      if (font->ok())
        font->drawText(px, layout_.width, layout_.title_h, menu::kTitleMargin + 1, baseline,
                       title_, frameText());
      DataBuffer *buf = DataBuffer::create(layout_.width, layout_.title_h, std::move(px));
      wlr_scene_buffer *sb = wlr_scene_buffer_create(tree_, buf->base());
      wlr_buffer_drop(buf->base());
      wlr_scene_node_set_position(&sb->node, 0, 0);
      title_node_ = &sb->node;
    }
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) emitItem(i);
  }

  void Menu::emitItem(int i) {
    if (item_nodes_[i]) { wlr_scene_node_destroy(&item_nodes_[i]->node); item_nodes_[i] = nullptr; }
    const menu::Rect r = layout_.items[i];
    const MenuItem &it = items_[i];
    bt::TextRenderer *font = server_.titleFont();

    std::vector<uint32_t> px;
    if (it.separator()) {
      px = render(r.w, r.h, kFrameLook);
      const int y = r.h / 2;
      for (int x = menu::kItemIndent; x < r.w - menu::kItemIndent; ++x)
        setPx(px, r.w, r.h, x, y, sepColor());
    } else {
      const bool active = (i == active_) && it.enabled;
      px = render(r.w, r.h, active ? kHiliteLook : kFrameLook);
      const int baseline = std::max(0, (r.h - font->height()) / 2) + font->ascent();
      const bt::Color tc = !it.enabled ? disabledText() : (active ? activeText() : frameText());
      if (font->ok())
        font->drawText(px, r.w, r.h, menu::kItemIndent, baseline, it.label, tc);
      if (it.checked) drawCheck(px, r.w, r.h, tc);
    }
    DataBuffer *buf = DataBuffer::create(r.w, r.h, std::move(px));
    wlr_scene_buffer *sb = wlr_scene_buffer_create(tree_, buf->base());
    wlr_buffer_drop(buf->base());
    wlr_scene_node_set_position(&sb->node, r.x, r.y);
    item_nodes_[i] = sb;
  }

  void Menu::setActive(int index) {
    if (index == active_) return;
    const int old = active_;
    active_ = index;
    if (old >= 0 && old < static_cast<int>(items_.size())) emitItem(old);
    if (active_ >= 0 && active_ < static_cast<int>(items_.size())) emitItem(active_);
  }

  bool Menu::containsGlobal(int gx, int gy) const {
    return gx >= gx_ && gy >= gy_ && gx < gx_ + layout_.width && gy < gy_ + layout_.height;
  }

  int Menu::itemIndexAtGlobal(int gx, int gy) const {
    return menu::itemAt(layout_, gy - gy_, metrics_);
  }

} // namespace bbai
