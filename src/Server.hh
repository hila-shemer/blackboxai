// The compositor core: owns the wl_display, backend/renderer/allocator, the
// wlr_scene and output layout, and the five fixed scene layers. In M1 only the
// background layer is populated.
#ifndef BLACKBOXAI_SERVER_HH
#define BLACKBOXAI_SERVER_HH

#include "wlr.hpp"
#include "listener.hpp"
#include "Resource.hh"

namespace bbai {

  class Output;

  class Server {
  public:
    explicit Server(bool headless);
    ~Server();

    bool ok() const { return display != nullptr && backend != nullptr; }
    void run();        // wl_display_run (blocking)
    void terminate();  // wl_display_terminate
    bool dispatch();   // single non-blocking event-loop iteration (for tests)

    // test-only accessors (M1 has a single output)
    Output *activeOutputForTest() const { return active_output; }
    wlr_scene_output *activeSceneOutputForTest() const;

    wl_display *display = nullptr;
    wlr_backend *backend = nullptr;
    wlr_renderer *renderer = nullptr;
    wlr_allocator *allocator = nullptr;
    wlr_scene *scene = nullptr;
    wlr_output_layout *output_layout = nullptr;
    wlr_scene_output_layout *scene_layout = nullptr;

    // fixed layer order (bottom -> top): background, bottom, window, top, overlay
    wlr_scene_tree *layer_background = nullptr;
    wlr_scene_tree *layer_bottom = nullptr;
    wlr_scene_tree *layer_window = nullptr;
    wlr_scene_tree *layer_top = nullptr;
    wlr_scene_tree *layer_overlay = nullptr;

    bt::Resource style;  // desktop style driving the background texture

  private:
    bool headless = false;
    bt::Listener new_output;
    Output *active_output = nullptr;  // M1: single output
  };

} // namespace bbai

#endif // BLACKBOXAI_SERVER_HH
