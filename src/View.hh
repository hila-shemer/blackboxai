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
#include "StackingList.hh"

#include <memory>

namespace bbai {

  class Server;

  class View : public StackEntity {
  public:
    View(Server &server, wlr_xdg_toplevel *toplevel);
    ~View();

    void *windowID() const override { return const_cast<View *>(this); }

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

    // Which workspace this window belongs to (M4).
    unsigned workspace() const { return workspace_; }
    void setWorkspace(unsigned w) { workspace_ = w; }
    // Show/hide the whole frame (workspace switch) without losing stacking order.
    void setOnWorkspace(bool on);
    bool visible() const;

    // Iconified state: a minimised window is hidden regardless of workspace.
    void setIconified(bool i);
    bool isIconified() const { return iconified_; }

    // Maximized state: frame fills the work area (output minus the toolbar).
    // frameW x frameH is the target frame dimensions to fill.
    void setMaximized(bool m, int frameW, int frameH);
    bool isMaximized() const { return maximized_; }

    // xdg-decoration: a decoration object for this toplevel appeared. Decide and
    // schedule its mode (request SSD / honor CSD holdout).
    void attachDecoration(wlr_xdg_toplevel_decoration_v1 *deco);
    bool drawsFrame() const { return draw_frame; }
    // The decoration mode the client has acked (0 NONE / 1 CLIENT_SIDE /
    // 2 SERVER_SIDE), or -1 if there is no decoration object.
    int decorationMode() const;

    // Focus state — tracks whether this window is the keyboard focus.
    // setFocused is idempotent; triggers a relayout only for mapped SSD windows.
    void setFocused(bool f);
    bool isFocused() const { return focused_; }

  private:
    void relayout();             // (re)build decorations for the current size + focus
    void chooseDecorationMode(); // the SSD/CSD rule; safe to call repeatedly
    void applyVisibility();      // sync frame_tree enable from on_workspace_ + iconified_

    Server &server;
    wlr_xdg_toplevel *xdg_toplevel;
    wlr_scene_tree *frame_tree = nullptr;    // owned (under layer_window)
    wlr_scene_tree *surface_tree = nullptr;  // xdg surface subtree (wlroots-owned)
    std::unique_ptr<Decoration> deco;
    wlr_xdg_toplevel_decoration_v1 *decoration = nullptr;
    unsigned workspace_ = 0;  // owning workspace (M4)
    bool on_workspace_ = true;  // last value passed to setOnWorkspace
    bool iconified_ = false;    // minimised state; hides frame regardless of workspace
    bool maximized_ = false;    // frame fills the work area
    int premax_x = 0, premax_y = 0, premax_w = 0, premax_h = 0;
    int pos_x = 160, pos_y = 120;
    int cw = 200, ch = 150;   // requested content size
    int laid_w = -1, laid_h = -1;  // size the decorations were last built for
    bool laid_frame = false;       // whether the last layout drew the frame
    bool mapped = false;
    bool draw_frame = true;   // default SSD; a CLIENT_SIDE request flips this off
    bool focused_ = false;
    bt::Listener map_, unmap_, commit_, destroy_;
    bt::Listener deco_request_mode_, deco_destroy_;
  };

} // namespace bbai

#endif // BLACKBOXAI_VIEW_HH
