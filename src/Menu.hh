// The runtime root menu: compositor chrome on layer_overlay, built from
// MenuItems via the renderer seam and the pure Menu.geom layout. M4 ships a
// flat menu (title + exec items + inline workspace entries + Restart/Exit);
// cascade submenus + menu-file parsing are M5. Modality lives on the Server
// (active_menu_ != nullptr gates the pointer/key handlers), exactly like the
// move/resize CursorMode grab.
#ifndef BLACKBOXAI_MENU_HH
#define BLACKBOXAI_MENU_HH

#include "wlr.hpp"
#include "Menu.geom.hh"
#include "MenuItem.hh"

#include <memory>
#include <string>
#include <vector>

namespace bbai {

  class Server;

  class Menu {
  public:
    Menu(Server &server, std::u32string title, std::vector<MenuItem> items);
    ~Menu();
    Menu(const Menu &) = delete;
    Menu &operator=(const Menu &) = delete;

    void show(int gx, int gy);            // position (clamped to output) + render
    bool containsGlobal(int gx, int gy) const;
    int itemIndexAtGlobal(int gx, int gy) const;  // selectable item index, else -1
    void setActive(int index);            // highlight (re-emits only changed rows)

    void openSubmenuAt(int index);
    void closeSubmenu();

    int itemCount() const { return static_cast<int>(items_.size()); }
    const MenuItem &item(int i) const { return items_[i]; }
    int activeIndex() const { return active_; }
    int rectXForTest() const { return gx_; }
    int rectYForTest() const { return gy_; }

    bool submenuOpenForTest() const { return static_cast<bool>(child_); }
    int  openSubmenuIndexForTest() const { return open_sub_; }
    int  submenuItemCountForTest() const { return child_ ? child_->itemCount() : 0; }
    int  childRectXForTest() const { return child_ ? child_->rectXForTest() : -1; }
    int  childRectYForTest() const { return child_ ? child_->rectYForTest() : -1; }

  private:
    void emitItem(int i);                 // (re)build one item's scene buffer
    Server &server_;
    wlr_scene_tree *tree_ = nullptr;
    std::u32string title_;
    std::vector<MenuItem> items_;
    std::vector<menu::ItemMetric> metrics_;
    menu::Layout layout_;
    int gx_ = 0, gy_ = 0;
    int active_ = -1;
    wlr_scene_node *frame_node_ = nullptr;
    wlr_scene_node *title_node_ = nullptr;
    std::vector<wlr_scene_buffer *> item_nodes_;  // per-item, for O(1) re-emit

    Menu *parent_ = nullptr;
    std::unique_ptr<Menu> child_;
    int open_sub_ = -1;
  };

} // namespace bbai

#endif // BLACKBOXAI_MENU_HH
