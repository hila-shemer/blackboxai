#include "Server.hh"
#include "Output.hh"
#include "View.hh"
#include "Toolbar.hh"
#include "Keyboard.hh"
#include "Menu.hh"
#include "Rootmenu.hh"
#include "Frame.hh"
#include "Screenshot.hh"
#include "ClipboardImage.hh"
#include "Autostart.hh"

#include <memory>

#include <algorithm>
#include <cstdio>                      // fprintf(stderr) loud-fail line
#include <cstdlib>                     // getenv/setenv (autostart)
#include <fstream>                     // read .desktop files
#include <set>                         // basename dedup (user shadows system)
#include <sstream>
#include <dirent.h>                    // opendir/readdir glob
#include <unistd.h>                    // access(X_OK) for TryExec
#include <linux/input-event-codes.h>   // BTN_LEFT / BTN_RIGHT

namespace {
  // Is `node` somewhere under the given scene layer tree?
  bool isUnder(wlr_scene_node *node, wlr_scene_tree *layer) {
    if (!node) return false;
    for (wlr_scene_tree *t = node->parent; t; t = t->node.parent)
      if (t == layer) return true;
    return false;
  }

  // TryExec resolution: an absolute/relative path is X_OK-checked as-is; a bare
  // name is searched along PATH. Mirrors what a launcher does before spawning.
  bool onPath(const std::string &exe) {
    if (exe.empty()) return false;
    if (exe.find('/') != std::string::npos)
      return access(exe.c_str(), X_OK) == 0;
    const char *path = getenv("PATH");
    if (!path) return false;
    std::stringstream ss(path);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
      if (dir.empty()) continue;
      if (access((dir + "/" + exe).c_str(), X_OK) == 0) return true;
    }
    return false;
  }

  std::string readFileToString(const std::string &path) {
    std::ifstream f(path);
    if (!f.good()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
  }
}

namespace bbai {

  Server::Server(bool hl) : headless(hl), title_font("monospace", 16) {
    wlr_log_init(WLR_ERROR, nullptr);

    display = wl_display_create();
    wl_event_loop *loop = wl_display_get_event_loop(display);

    backend = headless ? wlr_headless_backend_create(loop)
                       : wlr_backend_autocreate(loop, &session_);
    if (!backend) {
      wl_display_destroy(display);
      display = nullptr;
      return;
    }

    // Retain the libseat session (DRM path only; null under nested/headless) and
    // re-render every output when we VT-switch back. Without this the screen can
    // return blank/stale on resume because nothing repaints. libseat already
    // pauses/resumes the devices - the missing piece is the repaint on the way
    // back. The active path is hand-verified on a TTY (Task 8): headless has no
    // session, so this whole block stays inert there.
    if (session_) {
      session_active.connect(&session_->events.active, [this](void *) {
        if (session_ && session_->active)
          for (Output *o : outputs_) o->scheduleFrame();
      });
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
      mru_.touch(v);                             // newest window is most-recently-used
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
      // Light up every head: an Output per monitor, laid out left-to-right by
      // the output_layout the Output ctor adds itself to. The first head
      // enumerated stays primary - it carries the toolbar and the work area;
      // the rest just render their background and can host windows.
      Output *o = new Output(*this, wlr_out);
      outputs_.push_back(o);
      if (!active_output) {
        active_output = o;
        // The toolbar spans the primary output; create it now that the mode is
        // set. (Also the re-plug path: if every head died, active_output is
        // null again and the next head becomes the new primary.)
        toolbar_ = std::make_unique<Toolbar>(*this, *o);
        // Give the pointer an image from frame one - otherwise it's invisible
        // over our own chrome until the Super+F7 flow happens to latch one.
        // Real-output only: headless asserts byte-exact goldens and has no
        // screen to point at.
        if (!headless)
          wlr_cursor_set_xcursor(cursor, xcursor_mgr, "default");
      }
    });

    if (const char *sock = wl_display_add_socket_auto(display))
      socket_name = sock;

    // Exec runner for menu actions (spawned children inherit our WAYLAND_DISPLAY).
    default_runner_ = std::make_unique<PosixCommandRunner>(socket_name);
    command_runner_ = default_runner_.get();

    started_ = wlr_backend_start(backend);
    if (!started_)
      fprintf(stderr, "blackboxai: backend failed to start - no seat, or DRM "
                      "master held by another session\n");

    // The headless backend never emits new_output on its own; ask it for the
    // fixed 1280x720 test output so the background actually composites.
    if (headless)
      wlr_headless_add_output(backend, 1280, 720);

    // Bring up the user's usual agents on a real login. Skipped under headless
    // (CI must not spawn the host's tray/polkit) - the wiring is driven there by
    // runAutostartForTest with a FakeCommandRunner instead.
    if (!headless)
      runAutostart();
  }

  Server::~Server() {
    tearing_down_ = true;   // outputs die inside wl_display_destroy below;
                            // erase-only handling, never re-home chrome
    // Tear down our scene-tracking objects before the wlroots stack: their
    // listeners point into backend/surface signals that wlr_*_finish asserts
    // are empty.
    session_active.disconnect();   // points into session->events; drop before backend finish
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
    destroyScreenshotOverlay(); // null-guarded: frees the dim overlay if a drag was live
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
    if (pressed_button_view_ == view) {
      pressed_button_view_ = nullptr;
      pressed_button_part_ = Part::None;
    }
    std::erase(icons_, view);
    mru_.erase(view);                 // drop from last-used order (and any frozen ring)
    std::erase(cycle_ring_, view);
    if (cycle_start_ == view) cycle_start_ = nullptr;
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

  wlr_scene_output *Server::activeSceneOutput() const {
    return active_output ? active_output->sceneOutput() : nullptr;
  }

  void Server::addHeadlessOutputForTest(int w, int h) {
    wlr_headless_add_output(backend, w, h);   // fires new_output on the next dispatch
  }

  void Server::destroyOutputForTest(int index) {
    wlr_output_destroy(outputs_[static_cast<size_t>(index)]->wlrOutput());
  }

  Output *Server::outputAt(double lx, double ly) {
    wlr_output *wo = wlr_output_layout_output_at(output_layout, lx, ly);
    if (!wo) return active_output;
    for (Output *o : outputs_)
      if (o->wlrOutput() == wo) return o;
    return active_output;   // a layout output we don't track - shouldn't happen
  }

  Output *Server::outputForView(const View *v) {
    const int fw = v->drawsFrame() ? frame::frameWidth(v->contentWidth())
                                   : v->contentWidth();
    const int fh = v->drawsFrame() ? frame::frameHeight(v->contentHeight())
                                   : v->contentHeight();
    return outputAt(v->x() + fw / 2.0, v->y() + fh / 2.0);
  }

  void Server::remaximizeViewsOn(Output *o) {
    if (!o) return;
    for (auto &v : views)
      if (v->isMaximized() && outputForView(v.get()) == o)
        v->remaximize(o->workArea());
  }

  void Server::onOutputDestroyed(Output *o) {
    const bool primary_died = (active_output == o);
    std::erase(outputs_, o);
    if (primary_died)
      active_output = outputs_.empty() ? nullptr : outputs_.front();
    if (tearing_down_) return;
    if (primary_died) {
      // The toolbar's registered strut points into `o` - tear it down while
      // `o` is still alive (we're inside its destroy handler), then rebuild
      // on the survivor. If no head survives, the next new_output re-creates
      // it (active_output is null again, so the primary branch re-fires).
      toolbar_.reset();
      if (active_output)
        toolbar_ = std::make_unique<Toolbar>(*this, *active_output);
    }
    // Windows that lived on the dead head now resolve to the fallback head -
    // snap maximized ones onto a real work area instead of a ghost rectangle.
    remaximizeViewsOn(active_output);
  }

  void Server::runAutostart() {
    // Children resolve OnlyShowIn/NotShowIn against our id; set it before spawn.
    setenv("XDG_CURRENT_DESKTOP", "Blackbox", 1);

    std::vector<std::string> dirs = autostart_dirs_;
    if (dirs.empty()) {                        // production default: user shadows system
      if (const char *home = getenv("HOME"))
        dirs.push_back(std::string(home) + "/.config/autostart");
      dirs.push_back("/etc/xdg/autostart");
    }

    std::set<std::string> seen;                // basenames claimed by an earlier dir
    for (const std::string &dir : dirs) {
      DIR *d = opendir(dir.c_str());
      if (!d) continue;                        // a missing autostart dir is normal
      std::vector<std::string> names;
      while (dirent *ent = readdir(d)) {
        std::string name = ent->d_name;
        if (name.size() > 8 && name.compare(name.size() - 8, 8, ".desktop") == 0)
          names.push_back(name);
      }
      closedir(d);
      std::sort(names.begin(), names.end());   // deterministic spawn order

      for (const std::string &name : names) {
        if (!seen.insert(name).second) continue;   // shadowed by an earlier dir
        DesktopEntry e = parseDesktopEntry(readFileToString(dir + "/" + name));
        if (!shouldAutostart(e, "Blackbox")) continue;
        if (!e.try_exec.empty() && !onPath(e.try_exec)) continue;
        std::string cmd = stripFieldCodes(e.exec);
        if (cmd.empty()) continue;             // no Exec line -> nothing to run
        commandRunner().run({"/bin/sh", "-c", cmd});
      }
    }
  }

  const char *Server::seatSelectionMimeForTest() const {
    wlr_data_source *s = seat->selection_source;
    if (!s || s->mime_types.size == 0) return nullptr;
    return *static_cast<char *const *>(s->mime_types.data);
  }

  const std::string &Server::toolbarWindowTitleForTest() const {
    return toolbar_->windowTitleForTest();
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
    if (in(iconifyButton(W, H)))  return Part::IconifyButton;
    if (in(maximizeButton(W, H))) return Part::MaximizeButton;
    if (in(closeButton(W, H)))    return Part::CloseButton;
    if (in(title(W, H)))     return Part::Titlebar;  // incl. the label (drag = move)
    return Part::None;
  }

  void Server::focusView(View *v, bool update_mru) {
    if (focused_view == v) return;   // already at the MRU front; nothing to re-order
    if (focused_view) {
      wlr_xdg_toplevel_set_activated(focused_view->toplevel(), false);
      focused_view->setFocused(false);
    }
    focused_view = v;
    wlr_xdg_toplevel_set_activated(v->toplevel(), true);
    v->setFocused(true);
    // Cycle previews focus with update_mru=false so the frozen ring isn't
    // scrambled on every Tab; only a real focus change (or the commit) reorders.
    if (update_mru) mru_.touch(v);
    if (wlr_keyboard *kb = wlr_seat_get_keyboard(seat))
      wlr_seat_keyboard_notify_enter(seat, v->toplevel()->base->surface,
                                     kb->keycodes, kb->num_keycodes, &kb->modifiers);
    if (toolbar_) toolbar_->redrawWindowLabel(v->toplevel()->title);
  }

  // The alt-tab candidate set: every mapped, non-iconified window across all
  // workspaces, in MRU order (spec §3). Off-workspace windows are still mapped +
  // not iconified (setOnWorkspace only toggles the scene node), so they belong.
  std::vector<View *> Server::visibleRing() const {
    std::vector<View *> r;
    for (View *v : mru_.snapshot())
      if (v->isMapped() && !v->isIconified()) r.push_back(v);
    return r;
  }

  void Server::cycleStep(int dir) {
    if (!cycling_) {
      // Start (spec §3.2): freeze the ring; 0 or 1 window is nothing to cycle.
      cycle_ring_ = visibleRing();
      if (cycle_ring_.size() < 2) { cycle_ring_.clear(); return; }
      cycle_start_ = focused_view;
      cycling_ = true;
      // Record the raw held modifier that opened the session so releasing *that*
      // one commits (§3.3) — the raw depressed bit, not the cleaned binding mask.
      cycle_mod_ = 0;
      if (wlr_keyboard *kb = wlr_seat_get_keyboard(seat)) {
        const uint32_t d = kb->modifiers.depressed;
        cycle_mod_ = (d & WLR_MODIFIER_ALT)  ? WLR_MODIFIER_ALT
                   : (d & WLR_MODIFIER_LOGO) ? WLR_MODIFIER_LOGO : 0;
      }
      // Anchor the index on the currently-focused window (the MRU front), then
      // fall through to step off it — forward lands on the next-most-recent.
      // With nothing focused (e.g. the current workspace was emptied) there is
      // no window to step *off*: anchor one behind the edge so the first step
      // lands on the ring front (forward) or back (backward), not past it.
      auto it = std::find(cycle_ring_.begin(), cycle_ring_.end(), cycle_start_);
      cycle_index_ = (it == cycle_ring_.end())
                   ? (dir >= 0 ? cycle_ring_.size() - 1 : 0)
                   : static_cast<std::size_t>(it - cycle_ring_.begin());
    }
    if (cycle_ring_.empty()) { cycling_ = false; return; }   // ring emptied mid-cycle
    // Step, skipping entries that went invisible after the freeze (unmapped or
    // iconified mid-session; destroy is already pruned by removeView). If no
    // visible candidate remains the session dissolves.
    const std::size_t n = cycle_ring_.size();
    for (std::size_t hops = 0; hops < n; ++hops) {
      cycle_index_ = Mru<View>::step(cycle_index_, dir, n);
      View *v = cycle_ring_[cycle_index_];
      if (!v->isMapped() || v->isIconified()) continue;
      raiseView(v);
      focusView(v, /*update_mru=*/false);   // preview only — do not reorder mru_
      return;
    }
    cycling_ = false;
    cycle_ring_.clear();
    cycle_start_ = nullptr;
    cycle_mod_ = 0;
  }

  void Server::commitCycle() {
    if (!cycling_) return;
    cycling_ = false;
    View *v = focused_view;
    View *start = cycle_start_;
    cycle_ring_.clear();
    cycle_start_ = nullptr;
    cycle_mod_ = 0;
    // Nothing sane to commit onto a window that went invisible mid-hold; the
    // step path already skips those, this covers "previewed, then vanished."
    if (!v || !v->isMapped() || v->isIconified()) return;
    const unsigned old_ws = workspaces_.current();
    if (v->workspace() != old_ws) {
      // The preview focused v while the old workspace was still current, so a
      // bare setCurrentWorkspace would (a) restore the incoming workspace's
      // *remembered* view — focusing and MRU-fronting the wrong window before
      // we correct it — and (b) record v, a foreign window, as the outgoing
      // workspace's memory. Point the incoming memory at v so the restore lands
      // on the commit target itself...
      workspaces_.setFocused(v->workspace(), v);
      setCurrentWorkspace(v->workspace());
      // ...and repair the outgoing memory to the session-start window (what was
      // actually in use there when the cycle began).
      workspaces_.setFocused(old_ws,
                             (start && start->workspace() == old_ws) ? start : nullptr);
    }
    focusView(v);   // no-op if the switch's restore already landed here
    // The preview focused v with the MRU suppressed, so focusView can early-
    // return without reordering — front it explicitly; touch is idempotent.
    mru_.touch(v);
  }

  void Server::cancelCycle() {
    if (!cycling_) return;
    cycling_ = false;
    View *start = cycle_start_;
    cycle_ring_.clear();
    cycle_start_ = nullptr;
    cycle_mod_ = 0;
    if (start) focusView(start);            // restore focus to the session-start window (§3.2)
    // Re-sync the seat so a modifier released during the modal session isn't left
    // stuck-down in the client (mirrors closeMenus / resyncSeatAfterScreenshot).
    if (wlr_keyboard *kb = wlr_seat_get_keyboard(seat))
      wlr_seat_keyboard_notify_modifiers(seat, &kb->modifiers);
  }

  void Server::beginScreenshot() {
    // Abort any in-progress move/resize grab before arming (same as openRootMenu),
    // so the grab's terminating release can't strand the window.
    if (cursor_mode != CursorMode::Passthrough) {
      if (cursor_mode == CursorMode::Resize && grabbed_view)
        wlr_xdg_toplevel_set_resizing(grabbed_view->toplevel(), false);
      grabbed_view = nullptr;
      resize_edges = 0;
    }
    cursor_mode = CursorMode::ScreenshotSelect;
    wlr_cursor_set_xcursor(cursor, xcursor_mgr, "crosshair");
    // Release any client-side implicit pointer grab (mirrors openRootMenu): a
    // client holding a button when the keyboard-triggered mode arms would
    // otherwise never see its button-up (we swallow releases while modal) and be
    // stranded mid-drag.
    wlr_seat_pointer_notify_clear_focus(seat);
  }

  // Leaving the modal mode: re-resolve pointer focus onto whatever the cursor is
  // now over, and re-sync modifiers so one released during the mode isn't left
  // stuck-down in the focused client (mirrors closeMenus). cursor_mode must
  // already be Passthrough so onPointerMotion doesn't re-enter the modal gate.
  void Server::resyncSeatAfterScreenshot() {
    onPointerMotion(nowMsec());
    if (wlr_keyboard *kb = wlr_seat_get_keyboard(seat))
      wlr_seat_keyboard_notify_modifiers(seat, &kb->modifiers);
  }

  void Server::updateScreenshotOverlay() {
    if (!screenshot_overlay_) return;
    int ow = 1280, oh = 720;
    activeOutputSize(ow, oh);
    screenshot::Rect sel = screenshot::clampToOutput(
      screenshot::fromCorners(screenshot_ax_, screenshot_ay_,
                              static_cast<int>(cursor->x), static_cast<int>(cursor->y)),
      ow, oh);
    screenshot::DimRects d = screenshot::dimRects(ow, oh, sel);
    const screenshot::Rect *boxes[4] = { &d.above, &d.below, &d.left, &d.right };
    for (int i = 0; i < 4; ++i) {
      const screenshot::Rect &b = *boxes[i];
      if (b.w <= 0 || b.h <= 0) {
        wlr_scene_node_set_enabled(&screenshot_dim_[i]->node, false);
        continue;
      }
      wlr_scene_node_set_enabled(&screenshot_dim_[i]->node, true);
      wlr_scene_rect_set_size(screenshot_dim_[i], b.w, b.h);
      wlr_scene_node_set_position(&screenshot_dim_[i]->node, b.x, b.y);
    }
  }

  void Server::destroyScreenshotOverlay() {
    if (!screenshot_overlay_) return;
    wlr_scene_node_destroy(&screenshot_overlay_->node);   // destroys the rects too
    screenshot_overlay_ = nullptr;
    for (auto &r : screenshot_dim_) r = nullptr;
  }

  void Server::cancelScreenshot() {
    destroyScreenshotOverlay();
    screenshot_dragging_ = false;
    cursor_mode = CursorMode::Passthrough;
    wlr_cursor_set_xcursor(cursor, xcursor_mgr, "default");
    resyncSeatAfterScreenshot();
  }

  void Server::finishScreenshot() {
    destroyScreenshotOverlay();                 // dim must be gone BEFORE the readback
    screenshot_dragging_ = false;
    cursor_mode = CursorMode::Passthrough;
    wlr_cursor_set_xcursor(cursor, xcursor_mgr, "default");
    resyncSeatAfterScreenshot();                // before any early return below

    int ow = 1280, oh = 720;
    activeOutputSize(ow, oh);
    screenshot::Rect sel = screenshot::clampToOutput(
      screenshot::fromCorners(screenshot_ax_, screenshot_ay_,
                              static_cast<int>(cursor->x), static_cast<int>(cursor->y)),
      ow, oh);
    if (sel.w < 4 || sel.h < 4) return;         // sub-pixel drag: treat as cancel

    int rw = 0, rh = 0;
    std::vector<uint32_t> px =
      screenshot::captureRegion(activeSceneOutput(), renderer, sel, rw, rh);
    if (px.empty()) return;                     // capture failed (e.g. GL read_pixels)
    std::vector<uint8_t> bytes = screenshot::encodePng(px, rw, rh);
    if (bytes.empty()) return;

    auto blob = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
    ClipboardImage *ci = ClipboardImage::create(display, blob);   // wlroots owns it
    wlr_seat_set_selection(seat, &ci->base, wl_display_next_serial(display));
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
    if (cursor_mode == CursorMode::ScreenshotSelect) {
      if (screenshot_dragging_) updateScreenshotOverlay();
      return;   // modal: no client/toolbar/grab handling while selecting
    }
    if (toolbar_) toolbar_->handlePointerMotion(cursor->x, cursor->y);   // auto-hide edge trigger (no-op when off)
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

    if (cursor_mode == CursorMode::ScreenshotSelect) {
      if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (button == BTN_RIGHT) { cancelScreenshot(); return; }   // right-click cancels
        if (button == BTN_LEFT && !screenshot_dragging_) {
          screenshot_dragging_ = true;
          screenshot_ax_ = static_cast<int>(cursor->x);
          screenshot_ay_ = static_cast<int>(cursor->y);
          screenshot_overlay_ = wlr_scene_tree_create(layer_overlay);
          const float dim[4] = { 0.f, 0.f, 0.f, 0.35f };   // premultiplied black
          for (auto &r : screenshot_dim_)
            r = wlr_scene_rect_create(screenshot_overlay_, 1, 1, dim);
          updateScreenshotOverlay();
        }
        return;
      }
      // RELEASED
      if (button == BTN_LEFT && screenshot_dragging_) finishScreenshot();
      return;   // own all releases while modal
    }

    if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
      if (cursor_mode != CursorMode::Passthrough) {  // end a move/resize grab
        if (cursor_mode == CursorMode::Resize && grabbed_view)
          wlr_xdg_toplevel_set_resizing(grabbed_view->toplevel(), false);
        cursor_mode = CursorMode::Passthrough;
        grabbed_view = nullptr;
        resize_edges = 0;
        return;  // swallow the terminating release
      }
      if (pressed_button_view_) {  // a title-bar button was pressed; check release-inside
        View *pv = pressed_button_view_;
        Part pp = pressed_button_part_;
        pressed_button_view_ = nullptr;
        pressed_button_part_ = Part::None;
        double sx = 0, sy = 0;
        wlr_scene_node *n = wlr_scene_node_at(&scene->tree.node, cursor->x, cursor->y, &sx, &sy);
        if (viewFromNode(n) == pv && partAt(pv, cursor->x, cursor->y) == pp)
          dispatchButtonRelease(pv, pp);
        return;  // swallow the release either way
      }
      // else: fall through to forward the release to the client
    }

    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
      // Right-click on the bare desktop opens the modal root menu.
      if (button == BTN_RIGHT && overDesktop(cursor->x, cursor->y)) {
        openRootMenu(cursor->x, cursor->y);
        return;
      }
      // Middle-click on the bare desktop opens the icon menu.
      if (button == BTN_MIDDLE && overDesktop(cursor->x, cursor->y)) {
        openIconMenu(cursor->x, cursor->y);
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
          if (part == Part::IconifyButton || part == Part::MaximizeButton || part == Part::CloseButton) {
            pressed_button_view_ = v;
            pressed_button_part_ = part;
            return;  // swallow press; dispatch action on release-inside
          }
        }
        // press on the client area falls through to forward to the client
      }
    }
    wlr_seat_pointer_notify_button(seat, time, button, state);
  }

  // --- title-bar button dispatch (F4.3+) ----------------------------------------

  void Server::dispatchButtonRelease(View *v, Part part) {
    switch (part) {
    case Part::IconifyButton: iconifyView(v); break;
    case Part::CloseButton: wlr_xdg_toplevel_send_close(v->toplevel()); break;
    case Part::MaximizeButton: {
      Output *o = outputForView(v);
      if (!o) break;   // zero outputs - nowhere to fill
      v->setMaximized(!v->isMaximized(), o->workArea());
      break;
    }
    default: break;
    }
  }

  void Server::iconifyView(View *v) {
    v->setIconified(true);            // hide first, so the re-home below skips it
    icons_.push_back(v);
    if (focused_view == v) {
      if (View *top = topmostViewOnWorkspace(workspaces_.current())) focusView(top);
      else clearFocus();
    }
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
      if (cursor_mode == CursorMode::ScreenshotSelect) {  // modal: Escape cancels
        for (int i = 0; i < nsyms; ++i)
          if (syms[i] == XKB_KEY_Escape) { cancelScreenshot(); break; }
        swallowed_keycodes_.insert(keycode);   // swallow the key (and its release)
        return;
      }
      if (cycling_) {  // modal: cycle steps and Escape act, every other key is swallowed
        for (int i = 0; i < nsyms; ++i) {
          if (syms[i] == XKB_KEY_Escape) { cancelCycle(); break; }
          // Only the cycle's own bindings run mid-session — a Super-opened cycle
          // must not fire WorkspaceNext/OpenMenu/Screenshot and stack a second
          // modal mode on top of this one.
          const Action a = keybindings_.dispatch(mods, syms[i]);
          if (a.kind == Action::CycleNext || a.kind == Action::CyclePrev) {
            last_action_ = a;
            executeAction(a);
            break;
          }
        }
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
    // Commit the alt-tab cycle the moment the modifier that opened it goes up
    // (spec §3.3). commitCycle clears cycling_, so the notify below re-syncs the
    // seat with the released modifier — no separate re-sync needed on this path.
    if (cycling_ && cycle_mod_ && !(kb->modifiers.depressed & cycle_mod_)) commitCycle();
    // Modal modes own the keyboard: don't leak modifier state to the focused
    // client (the menu gate and the screenshot mode both rely on this). The exit
    // paths re-sync, so a modifier released while modal isn't left stuck-down.
    if (active_menu_ || cursor_mode == CursorMode::ScreenshotSelect) return;
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
    case Action::IconMenu:  openIconMenu(cursor->x, cursor->y); break;
    case Action::Screenshot: beginScreenshot(); break;
    case Action::Quit:      terminate(); break;
    case Action::CycleNext: cycleStep(+1); break;
    case Action::CyclePrev: cycleStep(-1); break;
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
    if (toolbar_) toolbar_->redrawWindowLabel(nullptr);
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
      if (v->workspace() == ws && v->isMapped() && !v->isIconified()) return v;
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
    if (cursor_mode == CursorMode::ScreenshotSelect) {
      if (pressed && sym == XKB_KEY_Escape) cancelScreenshot();
      return;
    }
    if (cycling_) {  // mirror onKey's modal block: step/Escape act, all else swallowed
      if (!pressed) return;
      if (sym == XKB_KEY_Escape) { cancelCycle(); return; }
      const Action a = keybindings_.dispatch(mods, sym);
      if (a.kind == Action::CycleNext || a.kind == Action::CyclePrev) {
        last_action_ = a;
        executeAction(a);
      }
      return;
    }
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

  std::vector<MenuItem> Server::buildIconMenu() {
    std::vector<MenuItem> items;
    for (View *v : icons_) {
      MenuItem m;
      const char *t = v->toplevel()->title;
      m.label = bt::decodeUtf8(t && *t ? t : "(unnamed)");
      m.action = MenuItem::Act::Deiconify;
      m.target = v->windowID();
      items.push_back(std::move(m));
    }
    return items;
  }

  void Server::openIconMenu(double lx, double ly) {
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
    active_menu_ = std::make_unique<Menu>(*this, bt::decodeUtf8("Icons"), buildIconMenu());
    active_menu_->show(static_cast<int>(lx), static_cast<int>(ly));
    wlr_seat_pointer_notify_clear_focus(seat);   // input is modal while open
  }

  void Server::deiconifyView(View *v) {
    v->setIconified(false);
    // If the window was iconified on a different workspace, bring it to the
    // current one so it is visible before we raise and focus it. Without this,
    // on_workspace_ stays false and keyboard focus goes to an invisible window.
    if (v->workspace() != workspaces_.current()) {
      v->setWorkspace(workspaces_.current());
      v->setOnWorkspace(true);
    }
    raiseView(v);
    focusView(v);
    std::erase(icons_, v);
  }

  void Server::openIconMenuForTest() {
    openIconMenu(cursor->x, cursor->y);
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
    case MenuItem::Act::Deiconify:
      if (View *v = viewForHandle(copy.target)) deiconifyView(v);
      break;
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
