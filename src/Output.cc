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
      // Headless outputs advertise no mode list, but the backend already gave
      // them the size wlr_headless_add_output was called with - keep it, or a
      // second 800x600 test head would silently become another 1280x720 one.
      wlr_output_state_set_custom_mode(&state,
          output->width > 0 ? output->width : 1280,
          output->height > 0 ? output->height : 720, 0);
    wlr_output_state_set_scale(&state, 1);
    wlr_output_commit_state(output, &state);
    wlr_output_state_finish(&state);

    // Advertise the head to clients: ext_session_lock_v1.get_lock_surface takes
    // a wl_output, and until this slice nothing ever created the global - no
    // client could name an output at all. Compositor-wide behavior change,
    // landed early in the train on purpose.
    wlr_output_create_global(output, server.display);

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
    destroy.connect(&output->events.destroy, [this](void *) {
      server.onOutputDestroyed(this);
      delete this;
    });
  }

  Output::~Output() {
    if (bg) wlr_scene_node_destroy(&bg->node);
  }

  void Output::scheduleFrame() {
    wlr_output_schedule_frame(output);
  }

  wlr_box Output::fullBox() const {
    wlr_box box{};
    wlr_output_layout_get_box(server.output_layout, output, &box);
    return box;
  }

  wlr_box Output::workArea() const {
    return workarea::computeWorkArea(fullBox(), struts_);
  }

  void Output::addStrut(const Strut *s) { struts_.push_back(s); strutsChanged(); }

  void Output::removeStrut(const Strut *s) { std::erase(struts_, s); strutsChanged(); }

  void Output::strutsChanged() { server.remaximizeViewsOn(this); }

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
    repositionBackground();        // layer_background is layout-coordinate space
  }

  void Output::repositionBackground() {
    if (!bg) return;
    const wlr_box box = fullBox();
    if (box.width == 0) return;   // leaving the layout (destroy path) - moot
    wlr_scene_node_set_position(&bg->node, box.x, box.y);
  }

} // namespace bbai
