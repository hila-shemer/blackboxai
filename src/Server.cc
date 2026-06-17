#include "Server.hh"
#include "Output.hh"
#include "View.hh"
#include "Toolbar.hh"
#include "Keyboard.hh"
#include "Menu.hh"
#include "Rootmenu.hh"
#include "Frame.hh"

#include <algorithm>
#include <linux/input-event-codes.h>   // BTN_LEFT / BTN_RIGHT

namespace {
  // Is `node` somewhere under the given scene layer tree?
  bool isUnder(wlr_scene_node *node, wlr_scene_tree *layer) {
    if (!node) return false;
    for (wlr_scene_tree *t = node->parent; t; t = t->node.parent)
      if (t == layer) return true;
    return false;
  }
}

namespace bbai {

  Server::Server(bool hl) : headless(hl), title_font("monospace", 16) {
    wlr_log_init(WLR_ERROR, nullptr);

    display = wl_display_create();
    wl_event_loop *loop = wl_display_get_event_loop(display);

    backend = headless ? wlr_headless_backend_create(loop)
                       : wlr_backend_autocreate(loop, nullptr);
    if (!backend) {
      wl_display_destroy(display);
      display = nullptr;
      return;
    }

    renderer = wlr_renderer_autocreate(backend);
    wlr_renderer_init_wl_display(renderer, display);
    allocator = wlr_allocator_autocreate(backend, renderer);

    wlr_compositor_create(display, 5, renderer);
    wlr_subcompositor_create(display);
    wlr_data_device_manager_create(display);
    wlr_single_pixel_buffer_manager_v1_create(display);

    scene = wlr_scene_create();
    output_layout = wlr_output_layout_create(display);
    scene_layout = wlr_scene_attach_output_layout(scene, output_layout);

    layer_background = wlr_scene_tree_create(&scene->tree);
    layer_bottom     = wlr_scene_tree_create(&scene->tree);
    layer_window     = wlr_scene_tree_create(&scene->tree);
    layer_top        = wlr_scene_tree_create(&scene->tree);
    layer_overlay    = wlr_scene_tree_create(&scene->tree);

    // Default desktop style (overridable later by a real .blackboxrc).
    style.loadFromString("BlackboxAI.desktop: flat gradient diagonal\n"
                         "BlackboxAI.desktop.color:   #204060\n"
                         "BlackboxAI.desktop.colorTo: #6080a0\n");

    xdg_shell = wlr_xdg_shell_create(display, 6);
    new_xdg_toplevel.connect(&xdg_shell->events.new_toplevel, [this](void *data) {
      auto *toplevel = static_cast<wlr_xdg_toplevel *>(data);
      views.push_back(std::make_unique<View>(*this, toplevel));
      View *v = views.back().get();
      v->setWorkspace(workspaces_.current());
      v->setOnWorkspace(true);                   // new windows open on the current ws
      stacking_.insert(v);                       // top of its layer
    });

    // Decoration policy: request SSD (we draw the Blackbox frame), honor CSD
    // holdouts. The decoration object can arrive after the View, so route it to
    // the matching View by its toplevel back-pointer.
    xdg_decoration = wlr_xdg_decoration_manager_v1_create(display);
    new_toplevel_decoration.connect(&xdg_decoration->events.new_toplevel_decoration,
      [this](void *data) {
        auto *deco = static_cast<wlr_xdg_toplevel_decoration_v1 *>(data);
        for (auto &v : views)
          if (v->toplevel() == deco->toplevel) { v->attachDecoration(deco); break; }
      });

    // KDE server-decoration hedge (obsolete protocol): advertise default SERVER
    // so older GTK/Qt builds that probe it suppress their own CSD chrome.
    wlr_server_decoration_manager *kde = wlr_server_decoration_manager_create(display);
    wlr_server_decoration_manager_set_default_mode(
      kde, WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);

    // Seat + cursor. The seat advertises pointer+keyboard unconditionally (the
    // headless backend has no real devices, but tests inject pointer events and
    // clients still bind wl_pointer for focus). The cursor is a tracked layout
    // point used for scene hit-testing; it is not itself a scene node.
    seat = wlr_seat_create(display, "seat0");
    wlr_seat_set_capabilities(seat,
      WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
    cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor, output_layout);
    xcursor_mgr = wlr_xcursor_manager_create(nullptr, 24);

    // Real input devices (DRM/libinput backend) — never fires under headless.
    new_input.connect(&backend->events.new_input, [this](void *data) {
      auto *dev = static_cast<wlr_input_device *>(data);
      if (dev->type == WLR_INPUT_DEVICE_POINTER) {
        wlr_cursor_attach_input_device(cursor, dev);
      } else if (dev->type == WLR_INPUT_DEVICE_KEYBOARD) {
        wlr_keyboard *kb = wlr_keyboard_from_input_device(dev);
        xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        xkb_keymap *km = xkb_keymap_new_from_names(ctx, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (km) {                       // a broken/missing xkb config returns NULL
          wlr_keyboard_set_keymap(kb, km);
          xkb_keymap_unref(km);
        }
        xkb_context_unref(ctx);
        // Only wire a keyboard whose keymap actually compiled: without it
        // kb->xkb_state is NULL and onKey's xkb lookup would crash.
        if (kb->xkb_state) {
          wlr_keyboard_set_repeat_info(kb, 25, 600);
          keyboards_.push_back(std::make_unique<Keyboard>(*this, kb));
          wlr_seat_set_keyboard(seat, kb);
          // If a window was already focused before any keyboard existed (focusView
          // only sends keyboard.enter when the seat has a keyboard), push focus to
          // it now so a hot-plugged / late-enumerated keyboard delivers keys.
          if (focused_view)
            wlr_seat_keyboard_notify_enter(seat, focused_view->toplevel()->base->surface,
                                           kb->keycodes, kb->num_keycodes, &kb->modifiers);
        }
      }
    });

    // Real cursor events: move/warp the cursor point then run the same handler
    // bodies the test-injection API calls.
    cursor_motion.connect(&cursor->events.motion, [this](void *data) {
      auto *e = static_cast<wlr_pointer_motion_event *>(data);
      wlr_cursor_move(cursor, &e->pointer->base, e->delta_x, e->delta_y);
      onPointerMotion(e->time_msec);
    });
    cursor_motion_absolute.connect(&cursor->events.motion_absolute, [this](void *data) {
      auto *e = static_cast<wlr_pointer_motion_absolute_event *>(data);
      wlr_cursor_warp_absolute(cursor, &e->pointer->base, e->x, e->y);
      onPointerMotion(e->time_msec);
    });
    cursor_button.connect(&cursor->events.button, [this](void *data) {
      auto *e = static_cast<wlr_pointer_button_event *>(data);
      onPointerButton(e->time_msec, e->button, e->state);
    });
    cursor_frame.connect(&cursor->events.frame, [this](void *) {
      wlr_seat_pointer_notify_frame(seat);
    });

    // Clock + timer registry. Headless tests use a VirtualClock at a fixed UTC
    // epoch (14:05:00 -> "02:05 PM") and drive timers by hand via
    // advanceClockForTest, so the ticking clock is deterministic; production uses
    // the real clock + this display's event loop.
    if (headless)
      clock_ = std::make_unique<bt::VirtualClock>(/*wall=*/14 * 3600 + 5 * 60, /*now_ms=*/0);
    else
      clock_ = std::make_unique<bt::SystemClock>();
    timer_registry_ = std::make_unique<TimerRegistry>(*clock_, headless ? nullptr : loop);

    new_output.connect(&backend->events.new_output, [this](void *data) {
      auto *wlr_out = static_cast<wlr_output *>(data);
      if (!active_output) {                     // M1: a single output
        active_output = new Output(*this, wlr_out);
        // The toolbar spans this output; create it now that the mode is set.
        toolbar_ = std::make_unique<Toolbar>(*this, wlr_out->width, wlr_out->height);
      }
    });

    if (const char *sock = wl_display_add_socket_auto(display))
      socket_name = sock;

    // Exec runner for menu actions (spawned children inherit our WAYLAND_DISPLAY).
    default_runner_ = std::make_unique<PosixCommandRunner>(socket_name);
    command_runner_ = default_runner_.get();

    wlr_backend_start(backend);

    // The headless backend never emits new_output on its own; ask it for the
    // fixed 1280x720 test output so the background actually composites.
    if (headless)
      wlr_headless_add_output(backend, 1280, 720);
  }

  Server::~Server() {
    // Tear down our scene-tracking objects before the wlroots stack: their
    // listeners point into backend/surface signals that wlr_*_finish asserts
    // are empty.
    new_output.disconnect();
    new_xdg_toplevel.disconnect();
    new_toplevel_decoration.disconnect();
    new_input.disconnect();
    cursor_motion.disconnect();
    cursor_motion_absolute.disconnect();
    cursor_button.disconnect();
    cursor_frame.disconnect();
    keyboards_.clear();       // drops key/modifiers listeners before the backend finish
    active_menu_.reset();     // destroys its overlay scene tree
    views.clear();
    toolbar_.reset();         // destroys its scene tree + clock Timer (registry still alive)
    timer_registry_.reset();  // removes its wl_event_source before the loop dies
    if (cursor) wlr_cursor_destroy(cursor);
    if (xcursor_mgr) wlr_xcursor_manager_destroy(xcursor_mgr);
    if (display) {
      wl_display_destroy_clients(display);
      wl_display_destroy(display);  // fires display_destroy -> backend finish (incl. seat)
    }
  }

  void Server::removeView(View *view) {
    if (grabbed_view == view) {
      cursor_mode = CursorMode::Passthrough;
      grabbed_view = nullptr;
      resize_edges = 0;
    }
    const bool was_focused = (focused_view == view);
    if (was_focused) focused_view = nullptr;
    workspaces_.clearFocused(view);   // drop from every workspace's focus memory
    stacking_.remove(view);           // drop from the Z-order before the View dies
    auto it = std::find_if(views.begin(), views.end(),
                           [view](const std::unique_ptr<View> &v) { return v.get() == view; });
    if (it != views.end())
      views.erase(it);                // destroys the View; `view` is dangling after this

    // If the closed window held focus, hand it to the topmost survivor on the
    // current workspace (else clear it) — don't leave the desktop unfocused.
    if (was_focused) {
      if (View *top = topmostViewOnWorkspace(workspaces_.current())) focusView(top);
      else clearFocus();
    }
  }

  void Server::raiseView(View *view) {
    stacking_.raise(view);
    wlr_scene_node_raise_to_top(&view->sceneTree()->node);
  }

  void Server::lowerView(View *view) {
    stacking_.lower(view);
    wlr_scene_node_lower_to_bottom(&view->sceneTree()->node);
  }

  void Server::run() { wl_display_run(display); }

  void Server::terminate() { if (display) wl_display_terminate(display); }

  bool Server::dispatch() {
    wl_event_loop *loop = wl_display_get_event_loop(display);
    wl_display_flush_clients(display);
    return wl_event_loop_dispatch(loop, 0) >= 0;
  }

  wlr_scene_output *Server::activeSceneOutputForTest() const {
    return active_output ? active_output->sceneOutput() : nullptr;
  }

  // --- input: hit-test, focus, grab state machine -------------------------------

  View *Server::viewFromNode(wlr_scene_node *node) {
    while (node) {
      if (node->data) return static_cast<View *>(node->data);
      node = node->parent ? &node->parent->node : nullptr;
    }
    return nullptr;
  }

  Part Server::partAt(View *v, double lx, double ly) {
    using namespace frame;
    const int fx = static_cast<int>(lx) - v->x();
    const int fy = static_cast<int>(ly) - v->y();
    const int W = v->contentWidth(), H = v->contentHeight();

    if (!v->drawsFrame()) {  // CSD: only the client area, at the View origin
      return (fx >= 0 && fy >= 0 && fx < W && fy < H) ? Part::Client : Part::None;
    }
    auto in = [&](Rect r) { return fx >= r.x && fy >= r.y && fx < r.x + r.w && fy < r.y + r.h; };
    if (fx >= clientX() && fy >= clientY() && fx < clientX() + W && fy < clientY() + H)
      return Part::Client;
    if (in(leftGrip(W, H)))  return Part::LeftGrip;
    if (in(rightGrip(W, H))) return Part::RightGrip;
    if (in(closeButton(W, H)) || in(maximizeButton(W, H)) || in(iconifyButton(W, H)))
      return Part::Button;
    if (in(title(W, H)))     return Part::Titlebar;  // incl. the label (drag = move)
    return Part::None;
  }

  void Server::focusView(View *v) {
    if (focused_view == v) return;
    if (focused_view) {
      wlr_xdg_toplevel_set_activated(focused_view->toplevel(), false);
      focused_view->setFocused(false);
    }
    focused_view = v;
    wlr_xdg_toplevel_set_activated(v->toplevel(), true);
    v->setFocused(true);
    if (wlr_keyboard *kb = wlr_seat_get_keyboard(seat))
      wlr_seat_keyboard_notify_enter(seat, v->toplevel()->base->surface,
                                     kb->keycodes, kb->num_keycodes, &kb->modifiers);
  }

  void Server::beginInteractive(View *v, CursorMode mode, uint32_t edges) {
    grabbed_view = v;
    cursor_mode  = mode;
    grab_x = cursor->x;
    grab_y = cursor->y;
    grab_geo_x = v->x();
    grab_geo_y = v->y();
    grab_geo_w = v->contentWidth();
    grab_geo_h = v->contentHeight();
    resize_edges = edges;
    if (mode == CursorMode::Resize)
      wlr_xdg_toplevel_set_resizing(v->toplevel(), true);
  }

  void Server::processMove() {
    const int nx = grab_geo_x + static_cast<int>(cursor->x - grab_x);
    const int ny = grab_geo_y + static_cast<int>(cursor->y - grab_y);
    grabbed_view->setPosition(nx, ny);
  }

  void Server::processResize() {
    const double dx = cursor->x - grab_x, dy = cursor->y - grab_y;
    const int right  = grab_geo_x + grab_geo_w;  // anchored when dragging LEFT
    const int bottom = grab_geo_y + grab_geo_h;  // anchored when dragging TOP
    int x = grab_geo_x, y = grab_geo_y, w = grab_geo_w, h = grab_geo_h;
    if (resize_edges & WLR_EDGE_LEFT)   w = grab_geo_w - static_cast<int>(dx);
    if (resize_edges & WLR_EDGE_RIGHT)  w = grab_geo_w + static_cast<int>(dx);
    if (resize_edges & WLR_EDGE_TOP)    h = grab_geo_h - static_cast<int>(dy);
    if (resize_edges & WLR_EDGE_BOTTOM) h = grab_geo_h + static_cast<int>(dy);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    // Re-derive the moving edge AFTER clamping so the opposite edge stays
    // anchored even when the size hits the 1px minimum (otherwise the window
    // would slide past the anchor on an over-shrink drag).
    if (resize_edges & WLR_EDGE_LEFT) x = right - w;
    if (resize_edges & WLR_EDGE_TOP)  y = bottom - h;
    grabbed_view->resizeTo(x, y, w, h);
  }

  void Server::onPointerMotion(uint32_t time) {
    if (active_menu_) {
      const int x = static_cast<int>(cursor->x), y = static_cast<int>(cursor->y);
      for (Menu *m = liveMenu(); m; m = m->parent()) {
        const int idx = m->itemIndexAtGlobal(x, y);
        if (idx >= 0) {
          m->setActive(idx);
          if (m->item(idx).kind == MenuItem::Kind::Submenu) m->openSubmenuAt(idx);
          else m->closeSubmenu();   // hovering a plain row in m drops m's stale child
          return;
        }
        if (m->containsGlobal(x, y)) { m->setActive(-1); return; }  // inside m, between items
      }
      // outside the whole chain: clear the deepest highlight but keep the menu open
      if (Menu *lm = liveMenu()) lm->setActive(-1);
      return;
    }
    if (cursor_mode == CursorMode::Move)   { processMove();   return; }
    if (cursor_mode == CursorMode::Resize) { processResize(); return; }

    // Implicit pointer grab: while a button is held over a client surface, keep
    // delivering motion to that surface even as the cursor crosses our chrome or
    // leaves the window (so client drag-select / scrollbar drags don't lose the
    // release). wlroots' default grab does NOT focus-lock, so we do it here.
    if (seat->pointer_state.button_count > 0 && focused_view &&
        seat->pointer_state.focused_surface == focused_view->toplevel()->base->surface) {
      const int ox = focused_view->x() + (focused_view->drawsFrame() ? frame::clientX() : 0);
      const int oy = focused_view->y() + (focused_view->drawsFrame() ? frame::clientY() : 0);
      wlr_seat_pointer_notify_motion(seat, time, cursor->x - ox, cursor->y - oy);
      return;
    }

    double sx = 0, sy = 0;
    wlr_scene_node *n = wlr_scene_node_at(&scene->tree.node, cursor->x, cursor->y, &sx, &sy);
    View *v = viewFromNode(n);
    if (v && partAt(v, cursor->x, cursor->y) == Part::Client) {
      wlr_surface *surf = v->toplevel()->base->surface;
      wlr_seat_pointer_notify_enter(seat, surf, sx, sy);
      wlr_seat_pointer_notify_motion(seat, time, sx, sy);
    } else {
      wlr_seat_pointer_notify_clear_focus(seat);
    }
  }

  void Server::onPointerButton(uint32_t time, uint32_t button,
                               wl_pointer_button_state state) {
    if (active_menu_) { handleMenuButton(button, state); return; }  // modal gate

    if (state == WL_POINTER_BUTTON_STATE_RELEASED && cursor_mode != CursorMode::Passthrough) {
      if (cursor_mode == CursorMode::Resize)
        wlr_xdg_toplevel_set_resizing(grabbed_view->toplevel(), false);
      cursor_mode = CursorMode::Passthrough;
      grabbed_view = nullptr;
      resize_edges = 0;
      return;  // swallow the terminating release
    }

    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
      // Right-click on the bare desktop opens the modal root menu.
      if (button == BTN_RIGHT && overDesktop(cursor->x, cursor->y)) {
        openRootMenu(cursor->x, cursor->y);
        return;
      }
      double sx = 0, sy = 0;
      wlr_scene_node *n = wlr_scene_node_at(&scene->tree.node, cursor->x, cursor->y, &sx, &sy);
      if (View *v = viewFromNode(n)) {
        const Part part = partAt(v, cursor->x, cursor->y);
        focusView(v);
        if (button == BTN_LEFT) {
          if (part == Part::Titlebar) { beginInteractive(v, CursorMode::Move, 0); return; }
          if (part == Part::LeftGrip)  { beginInteractive(v, CursorMode::Resize, WLR_EDGE_BOTTOM | WLR_EDGE_LEFT);  return; }
          if (part == Part::RightGrip) { beginInteractive(v, CursorMode::Resize, WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT); return; }
          if (part == Part::Button) { return; }  // buttons drawn; actions wired later
        }
        // press on the client area falls through to forward to the client
      }
    }
    wlr_seat_pointer_notify_button(seat, time, button, state);
  }

  // --- test-only injection + introspection --------------------------------------

  void Server::injectPointerMotionForTest(double lx, double ly) {
    wlr_cursor_warp(cursor, nullptr, lx, ly);
    onPointerMotion(nowMsec());
  }

  void Server::injectPointerButtonForTest(uint32_t button, bool pressed) {
    onPointerButton(nowMsec(), button,
                    pressed ? WL_POINTER_BUTTON_STATE_PRESSED
                            : WL_POINTER_BUTTON_STATE_RELEASED);
  }

  View *Server::viewAtForTest(double lx, double ly) {
    double sx = 0, sy = 0;
    return viewFromNode(wlr_scene_node_at(&scene->tree.node, lx, ly, &sx, &sy));
  }

  Part Server::partAtForTest(double lx, double ly) {
    View *v = viewAtForTest(lx, ly);
    return v ? partAt(v, lx, ly) : Part::None;
  }

  wlr_surface *Server::focusedPointerSurfaceForTest() const {
    return seat ? seat->pointer_state.focused_surface : nullptr;
  }

  // --- keyboard: bindings + focus forwarding ------------------------------------

  void Server::onKey(wlr_keyboard *kb, uint32_t time, uint32_t keycode,
                     wl_keyboard_key_state state) {
    if (!kb->xkb_state) return;   // defensive: a keymap-less device has no syms
    const xkb_keysym_t *syms = nullptr;
    const int nsyms = xkb_state_key_get_syms(kb->xkb_state, evdevToXkb(keycode), &syms);
    const uint32_t mods = wlr_keyboard_get_modifiers(kb);

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
      if (active_menu_) {  // modal: keys drive the menu, never the client
        for (int i = 0; i < nsyms; ++i)
          if (handleMenuKey(syms[i])) break;
        swallowed_keycodes_.insert(keycode);
        return;
      }
      for (int i = 0; i < nsyms; ++i) {
        if (dispatchBinding(mods, syms[i])) {
          swallowed_keycodes_.insert(keycode);  // also swallow the matching release
          return;
        }
      }
    } else if (swallowed_keycodes_.erase(keycode) > 0) {
      return;  // the press was a binding; don't deliver an orphan release
    }
    wlr_seat_set_keyboard(seat, kb);
    wlr_seat_keyboard_notify_key(seat, time, keycode, state);
  }

  void Server::onModifiers(wlr_keyboard *kb) {
    if (active_menu_) return;   // modal: don't leak modifier state to the client
    wlr_seat_set_keyboard(seat, kb);
    wlr_seat_keyboard_notify_modifiers(seat, &kb->modifiers);
  }

  void Server::removeKeyboard(Keyboard *k) {
    auto it = std::find_if(keyboards_.begin(), keyboards_.end(),
                           [k](const std::unique_ptr<Keyboard> &p) { return p.get() == k; });
    if (it != keyboards_.end()) keyboards_.erase(it);
  }

  bool Server::dispatchBinding(uint32_t mods, xkb_keysym_t sym) {
    Action a = keybindings_.dispatch(mods, sym);
    if (a.kind == Action::None) return false;
    last_action_ = a;
    executeAction(a);
    return true;
  }

  void Server::executeAction(const Action &a) {
    switch (a.kind) {
    case Action::WorkspaceNext: cycleWorkspace(+1); break;
    case Action::WorkspacePrev: cycleWorkspace(-1); break;
    case Action::WorkspaceTo:
      if (a.arg >= 0 && static_cast<unsigned>(a.arg) < workspaces_.count())
        setCurrentWorkspace(static_cast<unsigned>(a.arg));
      break;
    case Action::CloseWindow:
      if (focused_view) wlr_xdg_toplevel_send_close(focused_view->toplevel());
      break;
    case Action::OpenMenu:  openRootMenu(cursor->x, cursor->y); break;
    case Action::CycleNext: break;  // cycle focus within the workspace (future)
    case Action::CyclePrev: break;
    case Action::None:      break;
    }
  }

  void Server::cycleWorkspace(int delta) {
    const unsigned n = workspaces_.count();
    if (n == 0) return;
    const unsigned cur = workspaces_.current();
    setCurrentWorkspace((cur + (delta > 0 ? 1u : n - 1u)) % n);
  }

  void Server::clearFocus() {
    if (focused_view) {
      wlr_xdg_toplevel_set_activated(focused_view->toplevel(), false);
      focused_view->setFocused(false);
    }
    focused_view = nullptr;
    wlr_seat_keyboard_notify_clear_focus(seat);
  }

  View *Server::viewForHandle(void *handle) {
    if (!handle) return nullptr;
    for (auto &v : views)
      if (v.get() == handle) return v.get();
    return nullptr;
  }

  View *Server::topmostViewOnWorkspace(unsigned ws) {
    for (auto it = stacking_.begin(); it != stacking_.end(); ++it) {
      if (!*it) continue;                          // skip the layer sentinels
      View *v = static_cast<View *>(*it);
      if (v->workspace() == ws && v->isMapped()) return v;
    }
    return nullptr;
  }

  void Server::setCurrentWorkspace(unsigned i) {
    if (i >= workspaces_.count() || i == workspaces_.current()) return;

    // Remember the outgoing workspace's focus, then switch.
    workspaces_.setFocused(workspaces_.current(), focused_view);
    workspaces_.setCurrent(i);

    // Show the incoming workspace's views, hide the rest (O(1) per view, keeps
    // intra-layer Z-order).
    for (auto &v : views) v->setOnWorkspace(v->workspace() == i);

    // Restore focus for the incoming workspace: its remembered view if still
    // live + visible, else the topmost view on it, else nothing. Disabling a
    // scene node does NOT clear wlr_seat focus, so this must be explicit.
    View *restore = viewForHandle(workspaces_.focused(i));
    if (restore && restore->workspace() == i && restore->isMapped()) {
      focusView(restore);
    } else if (View *top = topmostViewOnWorkspace(i)) {
      focusView(top);
    } else {
      clearFocus();
    }
    onPointerMotion(nowMsec());   // refresh pointer focus off any hidden surface
    if (toolbar_) toolbar_->redrawWorkspaceLabel();
  }

  void Server::injectKeyForTest(xkb_keysym_t sym, uint32_t mods, bool pressed) {
    if (active_menu_) { if (pressed) handleMenuKey(sym); return; }
    if (pressed) dispatchBinding(mods, sym);
  }

  // --- modal root menu ----------------------------------------------------------

  void Server::activeOutputSize(int &w, int &h) const {
    if (wlr_scene_output *so = activeSceneOutputForTest()) {
      w = so->output->width; h = so->output->height;
    } else { w = 1280; h = 720; }
  }

  int Server::activeMenuItemForTest() const {
    return active_menu_ ? active_menu_->activeIndex() : -1;
  }

  bool Server::overDesktop(double lx, double ly) {
    double sx = 0, sy = 0;
    wlr_scene_node *n = wlr_scene_node_at(&scene->tree.node, lx, ly, &sx, &sy);
    if (!n) return true;                                       // nothing -> desktop
    if (viewFromNode(n)) return false;                         // a client window
    if (isUnder(n, layer_top) || isUnder(n, layer_overlay)) return false;  // chrome
    return true;                                               // background texture
  }

  void Server::openRootMenu(double lx, double ly) {
    if (active_menu_) return;
    // Abort any in-progress move/resize grab before going modal — otherwise the
    // grab's terminating release is swallowed by the modal gate and the window
    // would keep following the cursor after the menu closes.
    if (cursor_mode != CursorMode::Passthrough) {
      if (cursor_mode == CursorMode::Resize && grabbed_view)
        wlr_xdg_toplevel_set_resizing(grabbed_view->toplevel(), false);
      cursor_mode = CursorMode::Passthrough;
      grabbed_view = nullptr;
      resize_edges = 0;
    }
    active_menu_ = std::make_unique<Menu>(*this, rootmenu::title(),
                                          rootmenu::build(workspaces_));
    active_menu_->show(static_cast<int>(lx), static_cast<int>(ly));
    wlr_seat_pointer_notify_clear_focus(seat);   // input is modal while open
  }

  void Server::closeMenus() {
    active_menu_.reset();
    // While modal, onModifiers swallowed every modifier change so the client
    // wouldn't act on keys typed at the menu. Re-sync the seat now, or a modifier
    // released during the menu (e.g. the Mod4 that opened it via Mod4+space) stays
    // stuck-down in the still-focused client's view until its next transition.
    if (wlr_keyboard *kb = wlr_seat_get_keyboard(seat))
      wlr_seat_keyboard_notify_modifiers(seat, &kb->modifiers);
    onPointerMotion(nowMsec());   // restore normal pointer focus
  }

  Menu *Server::liveMenu() {
    Menu *m = active_menu_.get();
    if (!m) return nullptr;
    while (Menu *c = m->child()) m = c;
    return m;
  }

  void Server::activateMenuItem(const MenuItem &it) {
    const MenuItem copy = it;   // copy before closeMenus() destroys the owning Menu
    closeMenus();
    switch (copy.action) {
    case MenuItem::Act::Exec:            commandRunner().run(copy.argv); break;
    case MenuItem::Act::WorkspaceSwitch: setCurrentWorkspace(copy.workspace); break;
    case MenuItem::Act::NewWorkspace:    workspaces_.addWorkspace(); break;
    case MenuItem::Act::RemoveWorkspace: workspaces_.removeLastWorkspace(); break;
    case MenuItem::Act::Exit:            terminate(); break;
    case MenuItem::Act::Restart:         break;  // stub in M4
    case MenuItem::Act::None:            break;
    }
  }

  void Server::handleMenuButton(uint32_t, wl_pointer_button_state state) {
    if (state != WL_POINTER_BUTTON_STATE_PRESSED) return;  // activate on press
    const int x = static_cast<int>(cursor->x), y = static_cast<int>(cursor->y);
    for (Menu *m = liveMenu(); m; m = m->parent()) {
      const int idx = m->itemIndexAtGlobal(x, y);
      if (idx >= 0) {
        if (m->item(idx).kind == MenuItem::Kind::Submenu) { m->openSubmenuAt(idx); return; }
        activateMenuItem(m->item(idx));
        return;
      }
      if (m->containsGlobal(x, y)) return;   // inside this menu but not on an item: keep open
    }
    closeMenus();   // outside the whole chain
  }

  bool Server::handleMenuKey(xkb_keysym_t sym) {
    if (!active_menu_) return false;
    Menu *m = liveMenu();
    if (!m) { if (sym == XKB_KEY_Escape) closeMenus(); return true; }
    const int n = m->itemCount();
    if (n <= 0) { if (sym == XKB_KEY_Escape) closeMenus(); return true; }  // guard %n
    const int a = m->activeIndex();
    switch (sym) {
    case XKB_KEY_Escape:
      closeMenus();
      return true;
    case XKB_KEY_Down:
      for (int k = 1; k <= n; ++k) {
        const int j = ((a < 0 ? -1 : a) + k) % n;
        if (m->item(j).selectable()) { m->setActive(j); break; }
      }
      return true;
    case XKB_KEY_Up:
      for (int k = 1; k <= n; ++k) {
        const int j = (((a < 0 ? 0 : a) - k) % n + n) % n;
        if (m->item(j).selectable()) { m->setActive(j); break; }
      }
      return true;
    case XKB_KEY_Return:
    case XKB_KEY_space:
      if (a >= 0 && m->item(a).selectable()) {
        if (m->item(a).kind == MenuItem::Kind::Submenu) {
          m->openSubmenuAt(a);
          // set child active to its first selectable row
          Menu *child = m->child();
          if (child) {
            for (int j = 0; j < child->itemCount(); ++j) {
              if (child->item(j).selectable()) { child->setActive(j); break; }
            }
          }
        } else {
          activateMenuItem(m->item(a));
        }
      }
      return true;
    default:
      return true;   // swallow every key while the menu is modal
    }
  }

  void Server::itemClicked(int index) {
    if (!active_menu_) return;
    const MenuItem it = active_menu_->item(index);  // copy before activateMenuItem destroys the menu
    activateMenuItem(it);
  }

  void Server::advanceClockForTest(int64_t seconds) {
    if (auto *vc = dynamic_cast<bt::VirtualClock *>(clock_.get()))
      vc->advance(seconds);
    timer_registry_->fireDue(clock_->nowMs());
  }

  int64_t Server::wallSecondsForTest() const {
    return clock_->wallSeconds();
  }

} // namespace bbai
