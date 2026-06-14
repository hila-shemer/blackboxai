#include "Server.hh"
#include "Output.hh"

namespace bbai {

  Server::Server(bool hl) : headless(hl) {
    wlr_log_init(WLR_ERROR, nullptr);

    display = wl_display_create();
    wl_event_loop *loop = wl_display_get_event_loop(display);

    backend = headless ? wlr_headless_backend_create(loop)
                       : wlr_backend_autocreate(loop, nullptr);
    if (!backend) {
      wl_display_destroy(display);
      display = nullptr;
      return;
    }

    renderer = wlr_renderer_autocreate(backend);
    wlr_renderer_init_wl_display(renderer, display);
    allocator = wlr_allocator_autocreate(backend, renderer);

    wlr_compositor_create(display, 5, renderer);

    scene = wlr_scene_create();
    output_layout = wlr_output_layout_create(display);
    scene_layout = wlr_scene_attach_output_layout(scene, output_layout);

    layer_background = wlr_scene_tree_create(&scene->tree);
    layer_bottom     = wlr_scene_tree_create(&scene->tree);
    layer_window     = wlr_scene_tree_create(&scene->tree);
    layer_top        = wlr_scene_tree_create(&scene->tree);
    layer_overlay    = wlr_scene_tree_create(&scene->tree);

    // Default desktop style (overridable later by a real .blackboxrc).
    style.loadFromString("BlackboxAI.desktop: flat gradient diagonal\n"
                         "BlackboxAI.desktop.color:   #204060\n"
                         "BlackboxAI.desktop.colorTo: #6080a0\n");

    new_output.connect(&backend->events.new_output, [this](void *data) {
      auto *wlr_out = static_cast<wlr_output *>(data);
      if (!active_output)                       // M1: a single output
        active_output = new Output(*this, wlr_out);
    });

    wlr_backend_start(backend);

    // The headless backend never emits new_output on its own; ask it for the
    // fixed 1280x720 test output so the background actually composites.
    if (headless)
      wlr_headless_add_output(backend, 1280, 720);
  }

  Server::~Server() {
    // Stop listening on the backend before it is torn down: wlr_backend_finish
    // asserts its new_output signal has no listeners left. Member destructors
    // run after this body, so disconnect explicitly first.
    new_output.disconnect();
    if (display) {
      wl_display_destroy_clients(display);
      wl_display_destroy(display);  // fires display_destroy -> backend finish
    }
  }

  void Server::run() { wl_display_run(display); }

  void Server::terminate() { if (display) wl_display_terminate(display); }

  bool Server::dispatch() {
    wl_event_loop *loop = wl_display_get_event_loop(display);
    wl_display_flush_clients(display);
    return wl_event_loop_dispatch(loop, 0) >= 0;
  }

  wlr_scene_output *Server::activeSceneOutputForTest() const {
    return active_output ? active_output->sceneOutput() : nullptr;
  }

} // namespace bbai
