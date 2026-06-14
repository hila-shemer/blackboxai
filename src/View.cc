#include "View.hh"
#include "Server.hh"

namespace bbai {

  View::View(Server &srv, wlr_xdg_toplevel *tl) : server(srv), xdg_toplevel(tl) {
    // The scene tree is created and managed by wlroots: it tracks the surface
    // and is destroyed automatically when the xdg_surface is destroyed.
    scene_tree = wlr_scene_xdg_surface_create(server.layer_window, tl->base);
    wlr_scene_node_set_position(&scene_tree->node, pos_x, pos_y);

    wlr_surface *surface = tl->base->surface;

    commit_.connect(&surface->events.commit, [this](void *) {
      // The first commit after creation is where the client wants its initial
      // configure; reply with the fixed M2 size.
      if (xdg_toplevel->base->initial_commit)
        wlr_xdg_toplevel_set_size(xdg_toplevel, 200, 150);
    });
    map_.connect(&surface->events.map, [this](void *) { mapped = true; });
    unmap_.connect(&surface->events.unmap, [this](void *) { mapped = false; });
    destroy_.connect(&surface->events.destroy, [this](void *) {
      server.removeView(this);  // erases the owning unique_ptr -> deletes *this
    });
  }

  // ~View only needs to drop our listeners (bt::Listener does that in its own
  // dtor). The scene tree is owned by wlroots and must NOT be destroyed here.
  View::~View() = default;

} // namespace bbai
