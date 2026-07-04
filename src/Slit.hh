// The Blackbox slit as the SNI tray's pixel half: compositor chrome on
// layer_top rendering sni::Host items as slot-sized icon cells over the
// style's slit texture. Mirrors the Toolbar pattern - scene tree on the
// primary Output, registrant-owned Strut, 250ms auto-hide Timer leaving a
// margin sliver that still struts. Geometry lives in Slit.geom.hh (classic
// semantics); clicks route through Server's onPointerButton gate.
#ifndef BLACKBOXAI_SLIT_HH
#define BLACKBOXAI_SLIT_HH

#include "wlr.hpp"
#include "Slit.geom.hh"
#include "WorkArea.geom.hh"
#include "Config.hh"
#include "Sni.hh"
#include "Timer.hh"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bbai {

  class Server;
  class Output;

  // Icon-cell pipeline as free functions so the fallback ladder is
  // unit-testable without a Server. Returns PREMULTIPLIED slot*slot pixels:
  // bus pixmap -> theme PNG (added by the icon_name-fallback task) ->
  // placeholder ring.
  namespace sliticon {
    std::vector<uint32_t> cellPixels(const sni::Item &item, int slot);
  }

  class Slit {
  public:
    Slit(Server &server, Output &output);
    ~Slit();
    Slit(const Slit &) = delete;
    Slit &operator=(const Slit &) = delete;

    // Items or style changed: full rebuild (size, cells, position, strut).
    // Coarse on purpose - a tray is a handful of icons; partial redraw is
    // complexity with no bottleneck behind it.
    void refresh();
    void restyle() { refresh(); }

    // rc knobs (Server::applyConfig). alwaysOnTop is parsed-but-inert: all
    // chrome lives on layer_top already (program law, the one documented
    // deviation - the toolbar ignores its onTop the same way).
    void applyOptions(const SlitConfig &opts);

    // Shown frame rect, layout coords. PINNED wave-2 seam: the Server click
    // gate and menus' Slitmenu placement share this one source of truth.
    slit::Rect currentRect() const { return shown_; }

    bool containsGlobal(int gx, int gy) const;
    int itemIndexAtGlobal(int gx, int gy) const;   // -1 = frame gap / miss

    void handlePointerMotion(double x, double y);  // auto-hide hot zone
    bool hidden() const { return hidden_; }

    int itemCountForTest() const { return static_cast<int>(item_count_); }
    slit::Rect itemRectGlobalForTest(int i) const;
    SlitPlacement placementForTest() const { return placement_; }
    SlitDirection directionForTest() const { return direction_; }

  private:
    void clearNodes();
    void emit(slit::Rect r, std::vector<uint32_t> px);
    void applyPosition();
    void updateStrut();
    void setAutoHide(bool on);
    void onPointerOverSlit(bool over);
    void onHideTimeout();
    slit::Metrics metrics() const;

    static constexpr int kHideDelayMs = 250;   // the Toolbar's constant, same UX
    struct HideTick : TimeoutHandler {
      explicit HideTick(Slit *s) : sl(s) {}
      Slit *sl;
      void timeout() override { sl->onHideTimeout(); }
    };
    HideTick hide_handler_{ this };
    std::unique_ptr<Timer> hide_timer_;
    bool auto_hide_ = false;
    bool hidden_ = false;

    Server &server_;
    Output &output_;
    Strut strut_;                    // registered on output_ for our lifetime
    wlr_scene_tree *tree_;
    int ow_, oh_;
    SlitPlacement placement_ = SlitPlacement::CenterRight;
    SlitDirection direction_ = SlitDirection::Vertical;
    std::size_t item_count_ = 0;
    slit::Rect shown_{0, 0, 1, 1};
    slit::Rect hidden_rect_{0, 0, 1, 1};
    std::vector<wlr_scene_node *> nodes_;
  };

} // namespace bbai

#endif // BLACKBOXAI_SLIT_HH
