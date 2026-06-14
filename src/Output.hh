// One wlr_output + its wlr_scene_output. Configures a 1280x720 scale-1 mode,
// paints the Blackbox desktop texture into the background layer, and drives the
// frame loop. Self-destructs on the output's destroy event.
#ifndef BLACKBOXAI_OUTPUT_HH
#define BLACKBOXAI_OUTPUT_HH

#include "wlr.hpp"
#include "listener.hpp"

namespace bbai {

  class Server;

  class Output {
  public:
    Output(Server &server, wlr_output *output);
    ~Output();

    wlr_scene_output *sceneOutput() const { return scene_output; }
    wlr_output *wlrOutput() const { return output; }

  private:
    void renderBackground();

    Server &server;
    wlr_output *output;
    wlr_scene_output *scene_output = nullptr;
    wlr_scene_buffer *bg = nullptr;
    bt::Listener frame, destroy;
  };

} // namespace bbai

#endif // BLACKBOXAI_OUTPUT_HH
