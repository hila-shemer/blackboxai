#include "View.hh"
#include "Server.hh"
#include "Frame.hh"

namespace bbai {

  View::View(Server &srv, wlr_xdg_toplevel *tl) : server(srv), xdg_toplevel(tl) {
    // Own a frame tree under the window layer; tag it so a scene hit-test can
    // recover this View by walking parents to node.data.
    frame_tree = wlr_scene_tree_create(server.layer_window);
    frame_tree->node.data = this;

    // The client surface subtree lives INSIDE the frame, at the content origin
    // (inside the border, below the titlebar). wlroots owns this subtree and
    // auto-destroys it when the xdg surface dies.
    surface_tree = wlr_scene_xdg_surface_create(frame_tree, tl->base);

    deco = std::make_unique<Decoration>(frame_tree, server.titleFont());
    wlr_scene_node_set_position(&frame_tree->node, pos_x, pos_y);

    wlr_surface *surface = tl->base->surface;

    commit_.connect(&surface->events.commit, [this](void *) {
      // First commit after creation wants the initial configure; schedule the
      // decoration mode and the fixed M3 content size into one atomic configure.
      if (xdg_toplevel->base->initial_commit) {
        chooseDecorationMode();
        wlr_xdg_toplevel_set_size(xdg_toplevel, cw, ch);
        return;
      }
      // A later commit at a new size (interactive resize) re-lays-out the frame.
      // NOTE: M3 keys the frame size off the *requested* cw/ch, which the M3
      // test client always honors. A client that clamps to its own min/max size
      // would commit a different geometry, leaving the frame sized to the
      // unfulfilled request — reconciling against the committed surface geometry
      // belongs with min/max-size handling in M4.
      if (mapped && (cw != laid_w || ch != laid_h || draw_frame != laid_frame))
        relayout();
    });
    map_.connect(&surface->events.map, [this](void *) {
      mapped = true;
      relayout();
    });
    unmap_.connect(&surface->events.unmap, [this](void *) {
      mapped = false;
      deco->clear();
    });
    destroy_.connect(&surface->events.destroy, [this](void *) {
      server.removeView(this);  // erases the owning unique_ptr -> deletes *this
    });
  }

  // wlroots auto-destroys surface_tree when the xdg surface dies; we own
  // everything else. Drop the decoration buffers first, then the (now child-free
  // of decorations) frame tree.
  View::~View() {
    deco.reset();
    if (frame_tree) wlr_scene_node_destroy(&frame_tree->node);
  }

  void View::relayout() {
    if (draw_frame) {
      wlr_scene_node_set_position(&surface_tree->node, frame::clientX(), frame::clientY());
      deco->rebuild(cw, ch, xdg_toplevel->title);
    } else {
      // CSD holdout: no chrome, client surface at the View origin; we still own
      // the scene tree and manage geometry.
      wlr_scene_node_set_position(&surface_tree->node, 0, 0);
      deco->clear();
    }
    laid_w = cw;
    laid_h = ch;
    laid_frame = draw_frame;
  }

  void View::setPosition(int x, int y) {
    pos_x = x;
    pos_y = y;
    wlr_scene_node_set_position(&frame_tree->node, x, y);
  }

  void View::setOnWorkspace(bool on) {
    wlr_scene_node_set_enabled(&frame_tree->node, on);
  }

  bool View::visible() const { return frame_tree->node.enabled; }

  void View::resizeTo(int x, int y, int w, int h) {
    cw = w;
    ch = h;
    setPosition(x, y);
    wlr_xdg_toplevel_set_size(xdg_toplevel, w, h);
    // The client redraws + commits asynchronously; the commit handler re-lays-out
    // the decoration frame at the new size (M3 resize task).
  }

  void View::attachDecoration(wlr_xdg_toplevel_decoration_v1 *d) {
    decoration = d;
    deco_request_mode_.connect(&d->events.request_mode, [this](void *) {
      chooseDecorationMode();
      if (mapped) relayout();
    });
    deco_destroy_.connect(&d->events.destroy, [this](void *) {
      // The decoration object is going away; drop our listeners and forget it.
      // Keep the current draw_frame (don't surprise-redecorate a CSD client).
      deco_request_mode_.disconnect();
      deco_destroy_.disconnect();
      decoration = nullptr;
    });
    chooseDecorationMode();
    if (mapped) relayout();
  }

  void View::chooseDecorationMode() {
    if (!decoration) {            // no negotiation channel -> our default is SSD
      draw_frame = true;
      return;
    }
    const auto req = decoration->requested_mode;
    const auto chosen = (req == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE)
        ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE   // honor the CSD holdout
        : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;  // NONE/SERVER -> SSD
    draw_frame = (chosen == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    // set_mode schedules an xdg configure, which asserts unless the surface is
    // initialized. A decoration can arrive before the first commit; in that case
    // skip here — the initial_commit handler re-runs this once initialized.
    if (xdg_toplevel->base->initialized)
      wlr_xdg_toplevel_decoration_v1_set_mode(decoration, chosen);
  }

  int View::decorationMode() const {
    return decoration ? static_cast<int>(decoration->current.mode) : -1;
  }

  void View::setFocused(bool f) {
    if (focused_ == f) return;     // idempotent
    focused_ = f;
    if (mapped && draw_frame) relayout();
  }

} // namespace bbai
