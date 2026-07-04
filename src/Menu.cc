#include "Menu.hh"
#include "Server.hh"
#include "Output.hh"
#include "DataBuffer.hh"
#include "Style.hh"

#include "Texture.hh"
#include "Image.hh"
#include "Text.hh"

#include <algorithm>

namespace bbai {

  namespace {
    std::vector<uint32_t> render(int w, int h, const bt::Texture &t) {
      return bt::Image(w, h).renderBuffer(t);
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
    void drawArrow(std::vector<uint32_t> &px, int w, int h, const bt::Color &c) {
      const int x0 = w - 9, cy = h / 2;           // right gutter triangle
      for (int d = 0; d <= 4; ++d)
        for (int dy = -(4 - d); dy <= (4 - d); ++dy) setPx(px, w, h, x0 + d, cy + dy, c);
    }
  } // namespace

  Menu::Menu(Server &server, std::u32string title, std::vector<MenuItem> items,
             bool show_title)
    : server_(server), title_(std::move(title)), show_title_(show_title),
      items_(std::move(items)) {
    tree_ = wlr_scene_tree_create(server_.layer_overlay);
    std::shared_ptr<const Style> st = server_.currentStyle();
    const MenuLook &look = st->menuLook();
    bt::TextRenderer *frame_font = st->menuFrameFont();
    bt::TextRenderer *title_font = st->menuTitleFont();
    metrics_.reserve(items_.size());
    for (const MenuItem &it : items_)
      metrics_.push_back({ it.separator() ? 0 : frame_font->textWidth(it.label), it.separator() });
    layout_ = menu::computeLayout(metrics_, frame_font->height(), show_title_,
                                  title_font->textWidth(title_), title_font->height(),
                                  look.frameMargin, look.titleMargin);
    item_nodes_.assign(items_.size(), nullptr);
  }

  Menu::~Menu() {
    if (tree_) wlr_scene_node_destroy(&tree_->node);  // destroys all children
  }

  void Menu::show(int gx, int gy) {
    // Clamp so the whole menu stays on the output UNDER the requested point,
    // in layout coords. Pre-fix this used the primary's size at origin (0,0),
    // which snapped second-head menus back to head 1. outputAt floors to the
    // active output, so a fallback box only bites when there is no output at
    // all (headless-no-output: keep the historical 1280x720).
    wlr_box b{0, 0, 1280, 720};
    if (Output *o = server_.outputAt(gx, gy)) b = o->fullBox();
    if (gx + layout_.width  > b.x + b.width)  gx = b.x + b.width  - layout_.width;
    if (gy + layout_.height > b.y + b.height) gy = b.y + b.height - layout_.height;
    if (gx < b.x) gx = b.x;
    if (gy < b.y) gy = b.y;
    gx_ = gx; gy_ = gy;
    wlr_scene_node_set_position(&tree_->node, gx_, gy_);

    std::shared_ptr<const Style> st = server_.currentStyle();
    const MenuLook &look = st->menuLook();
    bt::TextRenderer *title_font = st->menuTitleFont();
    const int baseline =
      std::max(0, (layout_.title_h - title_font->height()) / 2) + title_font->ascent();

    // Frame background (lowest).
    {
      std::vector<uint32_t> px = render(layout_.width, layout_.height, look.frame);
      DataBuffer *buf = DataBuffer::create(layout_.width, layout_.height, std::move(px));
      wlr_scene_buffer *sb = wlr_scene_buffer_create(tree_, buf->base());
      wlr_buffer_drop(buf->base());
      wlr_scene_node_set_position(&sb->node, 0, 0);
      frame_node_ = &sb->node;
    }
    // Title bar (skipped for titleless menus - Windowmenu / dbusmenu).
    if (layout_.title_h > 0) {
      std::vector<uint32_t> px = render(layout_.width, layout_.title_h, look.title);
      if (title_font->ok())
        title_font->drawText(px, layout_.width, layout_.title_h, look.titleMargin + 1, baseline,
                             title_, look.titleText);
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
    std::shared_ptr<const Style> st = server_.currentStyle();
    const MenuLook &look = st->menuLook();
    bt::TextRenderer *font = st->menuFrameFont();

    std::vector<uint32_t> px;
    if (it.separator()) {
      px = render(r.w, r.h, look.frame);
      const int y = r.h / 2;
      for (int x = menu::kItemIndent; x < r.w - menu::kItemIndent; ++x)
        setPx(px, r.w, r.h, x, y, look.frameForeground);
    } else {
      const bool active = (i == active_) && it.enabled;
      px = render(r.w, r.h, active ? look.active : look.frame);
      const int baseline = std::max(0, (r.h - font->height()) / 2) + font->ascent();
      const bt::Color tc = !it.enabled ? look.frameDisabled
                                       : (active ? look.activeText : look.frameText);
      if (font->ok())
        font->drawText(px, r.w, r.h, menu::kItemIndent, baseline, it.label, tc);
      if (it.checked) drawCheck(px, r.w, r.h, tc);
      if (it.kind == MenuItem::Kind::Submenu) drawArrow(px, r.w, r.h, tc);
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

  void Menu::openSubmenuAt(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return;
    if (items_[index].kind != MenuItem::Kind::Submenu) return;
    if (open_sub_ == index && child_) return;            // already open
    closeSubmenu();
    child_ = std::make_unique<Menu>(server_, items_[index].label, items_[index].submenu_items);
    child_->parent_ = this;
    open_sub_ = index;
    // open to the right of the parent, aligned to the row top
    child_->show(gx_ + layout_.width, gy_ + layout_.items[index].y);
  }

  void Menu::closeSubmenu() { child_.reset(); open_sub_ = -1; }

  bool Menu::containsGlobal(int gx, int gy) const {
    return gx >= gx_ && gy >= gy_ && gx < gx_ + layout_.width && gy < gy_ + layout_.height;
  }

  int Menu::itemIndexAtGlobal(int gx, int gy) const {
    if (gx < gx_ || gx >= gx_ + layout_.width) return -1;   // honor the menu's X extent
    return menu::itemAt(layout_, gy - gy_, metrics_);
  }

} // namespace bbai
