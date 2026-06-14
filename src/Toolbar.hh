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

    toolbar::Rect barRectForTest(void) const { return toolbar::barRect(ow_, oh_); }

  private:
    void rebuild(void);
    void clearNodes(void);
    void emit(toolbar::Rect r, std::vector<uint32_t> px, bool is_clock = false);
    std::string clockText(void) const;

    Server &server_;
    wlr_scene_tree *tree_;
    int ow_, oh_;
    int label_w_ = 0, clock_w_ = 0;
    toolbar::Sections sections_{};
    std::vector<wlr_scene_node *> nodes_;   // all section nodes except the clock
    wlr_scene_buffer *clock_node_ = nullptr;
    std::string window_title_;
    std::unique_ptr<Timer> clock_timer_;
  };

} // namespace bbai

#endif // BLACKBOXAI_TOOLBAR_HH
