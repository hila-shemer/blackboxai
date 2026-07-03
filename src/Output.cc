#include "Output.hh"
#include "Server.hh"
#include "DataBuffer.hh"
#include "Image.hh"
#include "Texture.hh"
#include "Color.hh"
#include "Style.hh"

#include <ctime>
#include <memory>

namespace bbai {

  Output::Output(Server &srv, wlr_output *out) : server(srv), output(out) {
    wlr_output_init_render(output, server.allocator, server.renderer);

    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    if (wlr_output_mode *mode = wlr_output_preferred_mode(output))
      wlr_output_state_set_mode(&state, mode);
    else
      wlr_output_state_set_custom_mode(&state, 1280, 720, 0);  // headless
    wlr_output_state_set_scale(&state, 1);
    wlr_output_commit_state(output, &state);
    wlr_output_state_finish(&state);

    wlr_output_layout_output *lo =
      wlr_output_layout_add_auto(server.output_layout, output);
    scene_output = wlr_scene_output_create(server.scene, output);
    wlr_scene_output_layout_add_output(server.scene_layout, lo, scene_output);

    renderBackground();

    frame.connect(&output->events.frame, [this](void *) {
      wlr_scene_output_commit(scene_output, nullptr);
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      wlr_scene_output_send_frame_done(scene_output, &now);
    });
    destroy.connect(&output->events.destroy, [this](void *) { delete this; });
  }

  Output::~Output() {
    if (bg) wlr_scene_node_destroy(&bg->node);
  }

  void Output::scheduleFrame() {
    wlr_output_schedule_frame(output);
  }

  void Output::renderBackground() {
    const int w = output->width, h = output->height;
    if (bg) { wlr_scene_node_destroy(&bg->node); bg = nullptr; }

    std::shared_ptr<const Style> st = server.currentStyle();
    const DesktopBackground &d = st->desktop();
    std::vector<uint32_t> px =
      (d.kind == DesktopBackground::Kind::Modula)
        ? bsetroot::renderModula(w, h, d.modX, d.modY, d.modFg, d.modBg)
        : bt::Image(w, h).renderBuffer(d.texture);

    DataBuffer *buf = DataBuffer::create(w, h, std::move(px));
    bg = wlr_scene_buffer_create(server.layer_background, buf->base());
    wlr_buffer_drop(buf->base());  // scene_buffer took its own ref
    wlr_scene_node_set_position(&bg->node, 0, 0);
  }

} // namespace bbai
