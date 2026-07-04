// The Blackbox toolbar: compositor-owned chrome on the `top` scene layer showing
// the current workspace name, the focused window title, four (inert in M4) arrow
// buttons, and a ticking clock. Rendered through the M1 seam exactly like
// Decoration; the clock is its own scene buffer so a per-minute tick rebuilds
// only it. The 60s tick is driven by an injectable bbai::Timer on the Server's
// TimerRegistry (a VirtualClock in tests -> deterministic goldens).
#ifndef BLACKBOXAI_TOOLBAR_HH
#define BLACKBOXAI_TOOLBAR_HH

#include "wlr.hpp"
#include "Toolbar.geom.hh"
#include "Timer.hh"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bbai {

  class Server;

  class Toolbar : public TimeoutHandler {
  public:
    Toolbar(Server &server, int output_w, int output_h);
    ~Toolbar();
    Toolbar(const Toolbar &) = delete;
    Toolbar &operator=(const Toolbar &) = delete;

    void timeout(void) override;                  // 60s clock tick -> redrawClock
    void redrawClock(void);
    void redrawWorkspaceLabel(void);              // on workspace switch (Phase B)
    void redrawWindowLabel(const char *title);    // on focus change (null/"" -> blank)

    // Re-theme hook: full rebuild (textures/fonts/metrics re-read from the
    // server's current style) + reposition.
    void restyle() { rebuild(); }

    toolbar::Rect barRectForTest(void) const { return currentBarRect(); }
    toolbar::Placement placementForTest(void) const { return placement_; }
    const std::string &windowTitleForTest(void) const { return window_title_; }
    void setPlacementForTest(toolbar::Placement p) { placement_ = p; rebuild(); }

    // CONTRACT setters (work-area owns the real promotion: its versions also
    // update the registered Strut). These interim shims exist so our config
    // application is written against the final names - DELETE both at the
    // merge-train rebase over work-area; call sites stay.
    void setPlacement(toolbar::Placement p) { setPlacementForTest(p); }
    void setAutoHide(bool on) { setAutoHideForTest(on); }

    // Live bar geometry: style metrics + config width + current placement.
    // Work-area's Strut derives from this, never from the constexpr defaults.
    toolbar::Rect currentBarRect() const;

    void handlePointerMotion(double x, double y);
    void onPointerOverToolbar(bool over);          // edge-trigger from the compositor
    void setAutoHideForTest(bool on);              // enable auto-hide (no config yet)
    bool hiddenForTest(void) const { return hidden_; }

  private:
    void rebuild(void);
    void clearNodes(void);
    void emit(toolbar::Rect r, std::vector<uint32_t> px, bool is_clock = false);
    std::string clockText(void) const;
    void applyPosition(void);
    void onHideTimeout(void);

    static constexpr int kHideDelayMs = 250;
    struct HideTick : TimeoutHandler {
      explicit HideTick(Toolbar *t) : tb(t) {}
      Toolbar *tb;
      void timeout() override { tb->onHideTimeout(); }
    };
    HideTick hide_handler_{ this };
    std::unique_ptr<Timer> hide_timer_;
    bool auto_hide_ = false;
    bool hidden_ = false;

    Server &server_;
    wlr_scene_tree *tree_;
    int ow_, oh_;
    toolbar::Placement placement_ = toolbar::Placement::BottomCenter;
    int label_w_ = 0, clock_w_ = 0;
    toolbar::Sections sections_{};
    std::vector<wlr_scene_node *> nodes_;   // all section nodes except the clock
    wlr_scene_buffer *clock_node_ = nullptr;
    // Bar buffer kept between rebuild() and redrawClock() so parentrelative
    // sections (Gray) can crop their pixels out of it.
    std::vector<uint32_t> bar_px_;
    toolbar::Rect bar_rect_{0, 0, 1, 1};
    std::string window_title_;
    std::unique_ptr<Timer> clock_timer_;
  };

} // namespace bbai

#endif // BLACKBOXAI_TOOLBAR_HH
