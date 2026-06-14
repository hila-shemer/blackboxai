// A client window with its Blackbox server-side decoration frame. The View owns
// a frame scene tree under layer_window (tagged with node.data = this for
// hit-testing), parents the xdg-surface subtree inside it at the content origin,
// and drives a Decoration that draws the titlebar/handle/grips/border. M3 parks
// the window at a fixed position and a fixed content size; interactive
// move/resize arrive in later M3 tasks.
#ifndef BLACKBOXAI_VIEW_HH
#define BLACKBOXAI_VIEW_HH

#include "wlr.hpp"
#include "listener.hpp"
#include "Decoration.hh"

#include <memory>

namespace bbai {

  class Server;

  class View {
  public:
    View(Server &server, wlr_xdg_toplevel *toplevel);
    ~View();

    wlr_xdg_toplevel *toplevel() const { return xdg_toplevel; }
    bool isMapped() const { return mapped; }
    int x() const { return pos_x; }
    int y() const { return pos_y; }
    int contentWidth() const { return cw; }
    int contentHeight() const { return ch; }

    // Move the whole frame (and its decorations + client) to (x, y).
    void setPosition(int x, int y);
    // The frame scene tree (its node.data is this View) — for tests / hit-test.
    wlr_scene_tree *sceneTree() const { return frame_tree; }

  private:
    void relayout();  // (re)build decorations for the current size + focus

    Server &server;
    wlr_xdg_toplevel *xdg_toplevel;
    wlr_scene_tree *frame_tree = nullptr;    // owned (under layer_window)
    wlr_scene_tree *surface_tree = nullptr;  // xdg surface subtree (wlroots-owned)
    std::unique_ptr<Decoration> deco;
    int pos_x = 160, pos_y = 120;
    int cw = 200, ch = 150;   // content size (fixed in M3 until resize lands)
    bool mapped = false;
    bt::Listener map_, unmap_, commit_, destroy_;
  };

} // namespace bbai

#endif // BLACKBOXAI_VIEW_HH
