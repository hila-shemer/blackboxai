// The Blackbox server-side decoration frame for one View: titlebar + label text
// + three buttons + handle + two resize grips + side borders, each a
// scene node parented under the View's frame tree. Rebuildable so resize
// (new size) and focus changes re-lay-out the buffers; clear() drops them for
// CSD clients / unmapped windows. Every textured element flows through the M1
// renderer seam (bt::Texture -> bt::Image::renderBuffer -> DataBuffer ->
// wlr_scene_buffer), exactly like Output::renderBackground().
#ifndef BLACKBOXAI_DECORATION_HH
#define BLACKBOXAI_DECORATION_HH

#include "wlr.hpp"
#include "Frame.hh"
#include "Color.hh"

#include <string>
#include <vector>

namespace bt { class Texture; class TextRenderer; }

namespace bbai {

  enum class Part { None, Titlebar, Label, Button, LeftGrip, RightGrip, Client };

  class Decoration {
  public:
    // `parent` is the View's frame scene tree; `font` renders the title label
    // (may be null / not ok() — then the label shows no text).
    Decoration(wlr_scene_tree *parent, bt::TextRenderer *font);
    ~Decoration();
    Decoration(const Decoration &) = delete;
    Decoration &operator=(const Decoration &) = delete;

    // (Re)build every decoration buffer for a client content size W x H, focus
    // state, and window title (may be null). Destroys the previous buffers.
    void rebuild(int W, int H, bool focused, const char *titleText);
    // Drop all decoration buffers (CSD client / unmapped window).
    void clear();

    bool drawn() const { return !nodes.empty(); }

  private:
    // Wrap a finished ARGB8888 buffer as a tracked scene node at r.(x,y).
    void emit(frame::Rect r, std::vector<uint32_t> pixels);
    // A solid-color border line (wlr_scene_rect).
    void emitRect(frame::Rect r, const bt::Color &c);

    wlr_scene_tree *parent;
    bt::TextRenderer *font;
    std::vector<wlr_scene_node *> nodes;  // every node we created (to destroy on rebuild)
  };

} // namespace bbai

#endif // BLACKBOXAI_DECORATION_HH
