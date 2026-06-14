// An undecorated client window: a wlr_xdg_toplevel wrapped in a scene tree on
// the window layer. M2 parks it at a fixed position and pushes a fixed size;
// interactive move/resize/focus and the Blackbox decoration frame arrive in M3.
#ifndef BLACKBOXAI_VIEW_HH
#define BLACKBOXAI_VIEW_HH

#include "wlr.hpp"
#include "listener.hpp"

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

  private:
    Server &server;
    wlr_xdg_toplevel *xdg_toplevel;
    wlr_scene_tree *scene_tree = nullptr;  // owned by wlr_scene; dies with the surface
    int pos_x = 160, pos_y = 120;
    bool mapped = false;
    bt::Listener map_, unmap_, commit_, destroy_;
  };

} // namespace bbai

#endif // BLACKBOXAI_VIEW_HH
