#include "View.hh"
#include "Server.hh"
#include "Frame.hh"
#include "Style.hh"

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

    deco = std::make_unique<Decoration>(frame_tree);
    wlr_scene_node_set_position(&frame_tree->node, pos_x, pos_y);

    wlr_surface *surface = tl->base->surface;

    commit_.connect(&surface->events.commit, [this](void *) {
      // First commit after creation wants the initial configure; schedule the
      // decoration mode and the fixed M3 content size into one atomic configure.
      if (xdg_toplevel->base->initial_commit) {
        chooseDecorationMode();
        wlr_xdg_toplevel_set_size(xdg_toplevel, cw, ch);
        // A client can request state before the first commit (mpv --fs); the
        // request handlers deferred, so apply from here now that the surface is
        // initialized and set_* can schedule a configure.
        if (xdg_toplevel->requested.fullscreen) server.requestFullscreen(this);
        else if (xdg_toplevel->requested.maximized) server.requestMaximize(this);
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
      server.onViewMapped(this);
    });
    unmap_.connect(&surface->events.unmap, [this](void *) {
      mapped = false;
      deco->clear();
    });
    destroy_.connect(&surface->events.destroy, [this](void *) {
      server.removeView(this);  // erases the owning unique_ptr -> deletes *this
    });

    // xdg state requests: the protocol REQUIRES a configure in response even if
    // nothing changed (wlr_xdg_shell.h). We route the three we implement through
    // the Server (it resolves the output); request_move/resize are ack-only v1.
    req_fullscreen_.connect(&xdg_toplevel->events.request_fullscreen, [this](void *) {
      if (xdg_toplevel->base->initialized) server.requestFullscreen(this);
      // else: applied from the initial_commit block below (gotcha #13).
    });
    req_maximize_.connect(&xdg_toplevel->events.request_maximize, [this](void *) {
      if (xdg_toplevel->base->initialized) server.requestMaximize(this);
    });
    req_minimize_.connect(&xdg_toplevel->events.request_minimize, [this](void *) {
      if (xdg_toplevel->base->initialized) server.requestMinimize(this);
    });
    req_move_.connect(&xdg_toplevel->events.request_move, [this](void *) {});    // ack-only v1
    req_resize_.connect(&xdg_toplevel->events.request_resize, [this](void *) {}); // ack-only v1
    // Emitted at the top of destroy_xdg_toplevel, before it asserts every
    // request_* signal has no listeners - disconnect ours here so a client that
    // tears down the toplevel role (proxy) ahead of the wl_surface can't abort.
    toplevel_destroy_.connect(&xdg_toplevel->events.destroy, [this](void *) {
      req_maximize_.disconnect();
      req_fullscreen_.disconnect();
      req_minimize_.disconnect();
      req_move_.disconnect();
      req_resize_.disconnect();
      toplevel_destroy_.disconnect();
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
    // Fullscreen hides chrome regardless of the decoration mode (an SSD window
    // goes borderless while fullscreen, like a CSD holdout).
    const bool chrome = draw_frame && !fullscreen_;
    if (chrome) {
      const frame::FrameMetrics &m = server.currentStyle()->frameMetrics();
      wlr_scene_node_set_position(&surface_tree->node, frame::clientX(m), frame::clientY(m));
      deco->rebuild(*server.currentStyle(), cw, ch, xdg_toplevel->title, focused_);
    } else {
      // CSD holdout / fullscreen: no chrome, client surface at the View origin;
      // we still own the scene tree and manage geometry.
      wlr_scene_node_set_position(&surface_tree->node, 0, 0);
      deco->clear();
    }
    laid_w = cw;
    laid_h = ch;
    laid_frame = chrome;   // track the EFFECTIVE chrome state, not raw draw_frame
  }

  void View::setPosition(int x, int y) {
    pos_x = x;
    pos_y = y;
    wlr_scene_node_set_position(&frame_tree->node, x, y);
  }

  void View::setOnWorkspace(bool on) { on_workspace_ = on; applyVisibility(); }

  void View::setIconified(bool i) {
    if (iconified_ == i) return;
    iconified_ = i;
    applyVisibility();
  }

  void View::setMaximized(bool m, wlr_box work) {
    if (maximized_ == m) return;
    if (m) {
      premax_x = pos_x; premax_y = pos_y; premax_w = cw; premax_h = ch;
      maximized_ = true;
      applyMaximizedGeometry(work);
      wlr_xdg_toplevel_set_maximized(xdg_toplevel, true);
    } else {
      maximized_ = false;
      resizeTo(premax_x, premax_y, premax_w, premax_h);
      wlr_xdg_toplevel_set_maximized(xdg_toplevel, false);
    }
  }

  void View::applyMaximizedGeometry(wlr_box work) {
    const frame::FrameMetrics &fm = server.currentStyle()->frameMetrics();
    const int contentW = work.width - 2 * fm.border;
    const int contentH = work.height - fm.titleHeight - fm.handleHeight;
    resizeTo(work.x, work.y, contentW, contentH);
  }

  void View::remaximize(wlr_box work) {
    if (!maximized_) return;
    applyMaximizedGeometry(work);
  }

  void View::setFullscreen(bool on, wlr_box full) {
    if (fullscreen_ == on) return;
    if (on) {
      // One shared saved rect: don't clobber a maximize's premax, and don't
      // re-save our own on a redundant enter.
      if (!maximized_ && !fullscreen_) {
        premax_x = pos_x; premax_y = pos_y; premax_w = cw; premax_h = ch;
      }
      fullscreen_ = true;
      wlr_xdg_toplevel_set_fullscreen(xdg_toplevel, true);   // the mandated ack
      resizeTo(full.x, full.y, full.width, full.height);     // content == fullBox
    } else {
      fullscreen_ = false;
      wlr_xdg_toplevel_set_fullscreen(xdg_toplevel, false);
      // Maximized-underneath restore is the Server's job (work area); here we
      // only restore premax for the plain case.
      if (!maximized_) resizeTo(premax_x, premax_y, premax_w, premax_h);
    }
    relayout();   // re-run the frame/chrome branch for the new fullscreen_ state
  }

  void View::applyVisibility() {
    wlr_scene_node_set_enabled(&frame_tree->node, on_workspace_ && !iconified_);
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
