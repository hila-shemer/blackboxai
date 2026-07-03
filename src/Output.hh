// One wlr_output + its wlr_scene_output. Configures a 1280x720 scale-1 mode,
// paints the Blackbox desktop texture into the background layer, and drives the
// frame loop. Self-destructs on the output's destroy event.
#ifndef BLACKBOXAI_OUTPUT_HH
#define BLACKBOXAI_OUTPUT_HH

#include "wlr.hpp"
#include "listener.hpp"
#include "WorkArea.geom.hh"

#include <vector>

namespace bbai {

  class Server;

  class Output {
  public:
    Output(Server &server, wlr_output *output);
    ~Output();

    wlr_scene_output *sceneOutput() const { return scene_output; }
    wlr_output *wlrOutput() const { return output; }

    // Ask for a fresh frame - used on VT-switch resume so the screen repaints
    // instead of coming back stale (M7).
    void scheduleFrame();

    // This head's box in layout coordinates (the Output added itself to the
    // layout in the ctor, so the lookup always resolves). Wave-2 fullscreen
    // consumes this directly - never workArea().
    wlr_box fullBox() const;

    // Work-area seam. Registrants (toolbar now, slit in wave 2) own their
    // Strut: addStrut once, mutate in place on change, removeStrut before
    // the Strut dies. workArea() = fullBox minus max-per-edge, on demand.
    wlr_box workArea() const;
    void addStrut(const Strut *s);
    void removeStrut(const Strut *s);

  private:
    void renderBackground();

    Server &server;
    wlr_output *output;
    wlr_scene_output *scene_output = nullptr;
    wlr_scene_buffer *bg = nullptr;
    std::vector<const Strut *> struts_;
    bt::Listener frame, destroy;
  };

} // namespace bbai

#endif // BLACKBOXAI_OUTPUT_HH
