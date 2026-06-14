// Pure menu geometry — single-column layout math distilled from blackboxwm
// lib/Menu.cc (updateSize/positionRect/itemRect/titleRect), header-only and
// L0-unit-testable with mock text widths. The runtime Menu (B8) feeds real
// fcft text widths in and positions one scene buffer per item from the rects.
// Pinned M4 metrics (no style file): margins 1, item gutter 16.
#ifndef BLACKBOXAI_MENU_GEOM_HH
#define BLACKBOXAI_MENU_GEOM_HH

#include <algorithm>
#include <vector>

namespace bbai::menu {

  constexpr int kFrameMargin     = 1;
  constexpr int kItemMargin      = 1;
  constexpr int kTitleMargin     = 1;
  constexpr int kItemIndent      = 16;   // left/right gutter for check + submenu arrow
  constexpr int kSeparatorHeight = 3;    // borderWidth(1) + 2*frameMargin(1)
  constexpr int kMinItemWidth    = 20;

  struct Rect { int x, y, w, h; };
  struct ItemMetric { int text_w; bool separator; };

  inline int itemHeight(int text_h, bool sep) {
    return sep ? kSeparatorHeight
               : (std::max(text_h, kItemIndent) + 2 * kItemMargin);
  }
  inline int itemWidth(int text_w) { return text_w + 2 * (kItemIndent + kItemMargin); }
  inline int titleHeight(int text_h) { return text_h + 2 * kTitleMargin; }

  struct Layout {
    int width = 0;          // menu total width
    int height = 0;         // menu total height (title + framed items)
    int title_h = 0;        // 0 if no title bar
    std::vector<Rect> items;  // per-item rect, menu-local
  };

  // Single-column layout: column width = max(title, every item width); items
  // stack below the (optional) title bar.
  inline Layout computeLayout(const std::vector<ItemMetric> &items, int item_text_h,
                              bool show_title, int title_text_w, int title_text_h) {
    Layout L;
    L.title_h = show_title ? titleHeight(title_text_h) : 0;

    int w = kMinItemWidth;
    if (show_title) w = std::max(w, title_text_w + 2 * kTitleMargin);
    for (const ItemMetric &it : items)
      w = std::max(w, it.separator ? kMinItemWidth : itemWidth(it.text_w));

    int y = L.title_h + kFrameMargin;
    for (const ItemMetric &it : items) {
      const int h = itemHeight(item_text_h, it.separator);
      L.items.push_back({ kFrameMargin, y, w, h });
      y += h;
    }
    L.width = w + 2 * kFrameMargin;
    L.height = y + kFrameMargin;
    return L;
  }

  // The selectable item index at menu-local y, or -1 (separator / title / miss).
  inline int itemAt(const Layout &L, int y, const std::vector<ItemMetric> &items) {
    for (size_t i = 0; i < L.items.size(); ++i) {
      const Rect &r = L.items[i];
      if (y >= r.y && y < r.y + r.h)
        return items[i].separator ? -1 : static_cast<int>(i);
    }
    return -1;
  }

} // namespace bbai::menu

#endif // BLACKBOXAI_MENU_GEOM_HH
