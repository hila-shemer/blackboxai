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
    // Move to (x,y) and request a new content size (w,h); the decoration frame
    // re-lays-out when the client commits the new buffer.
    void resizeTo(int x, int y, int w, int h);
    // The frame scene tree (its node.data is this View) — for tests / hit-test.
    wlr_scene_tree *sceneTree() const { return frame_tree; }

    // xdg-decoration: a decoration object for this toplevel appeared. Decide and
    // schedule its mode (request SSD / honor CSD holdout).
    void attachDecoration(wlr_xdg_toplevel_decoration_v1 *deco);
    bool drawsFrame() const { return draw_frame; }
    // The decoration mode the client has acked (0 NONE / 1 CLIENT_SIDE /
    // 2 SERVER_SIDE), or -1 if there is no decoration object.
    int decorationMode() const;

  private:
    void relayout();             // (re)build decorations for the current size + focus
    void chooseDecorationMode(); // the SSD/CSD rule; safe to call repeatedly

    Server &server;
    wlr_xdg_toplevel *xdg_toplevel;
    wlr_scene_tree *frame_tree = nullptr;    // owned (under layer_window)
    wlr_scene_tree *surface_tree = nullptr;  // xdg surface subtree (wlroots-owned)
    std::unique_ptr<Decoration> deco;
    wlr_xdg_toplevel_decoration_v1 *decoration = nullptr;
    int pos_x = 160, pos_y = 120;
    int cw = 200, ch = 150;   // content size (fixed in M3 until resize lands)
    bool mapped = false;
    bool draw_frame = true;   // default SSD; a CLIENT_SIDE request flips this off
    bt::Listener map_, unmap_, commit_, destroy_;
    bt::Listener deco_request_mode_, deco_destroy_;
  };

} // namespace bbai

#endif // BLACKBOXAI_VIEW_HH
