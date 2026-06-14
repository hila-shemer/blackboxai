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
    wlr_scene_node_set_position(&surface_tree->node, frame::clientX(), frame::clientY());

    deco = std::make_unique<Decoration>(frame_tree, server.titleFont());
    wlr_scene_node_set_position(&frame_tree->node, pos_x, pos_y);

    wlr_surface *surface = tl->base->surface;

    commit_.connect(&surface->events.commit, [this](void *) {
      // First commit after creation wants the initial configure; reply with the
      // fixed M3 content size.
      if (xdg_toplevel->base->initial_commit)
        wlr_xdg_toplevel_set_size(xdg_toplevel, cw, ch);
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
    deco->rebuild(cw, ch, /*focused=*/true, xdg_toplevel->title);
  }

  void View::setPosition(int x, int y) {
    pos_x = x;
    pos_y = y;
    wlr_scene_node_set_position(&frame_tree->node, x, y);
  }

} // namespace bbai
