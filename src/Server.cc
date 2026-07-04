#include "Server.hh"
#include "Output.hh"
#include "View.hh"
#include "Toolbar.hh"
#include "Slit.hh"
#include "Keyboard.hh"
#include "Menu.hh"
#include "Rootmenu.hh"
#include "Windowmenu.hh"
#include "Barmenu.hh"
#include "SniMenu.hh"
#include "ConfigSpelling.hh"
#include "BarSpelling.hh"
#include "MenuParser.hh"
#include "Frame.hh"
#include "Placement.geom.hh"
#include "Screenshot.hh"
#include "ClipboardImage.hh"
#include "Autostart.hh"
#include "SniHost.hh"
#include "SessionLock.hh"

#include <memory>

#include <algorithm>
#include <cstdio>                      // fprintf(stderr) loud-fail line
#include <cstdlib>                     // getenv/setenv (autostart)
#include <fstream>                     // read .desktop files
#include <set>                         // basename dedup (user shadows system)
#include <sstream>
#include <dirent.h>                    // opendir/readdir glob
#include <sys/stat.h>                  // stat-on-open menu reload (classic checkMenu)
#include <unistd.h>                    // access(X_OK) for TryExec
#include <linux/input-event-codes.h>   // BTN_LEFT / BTN_RIGHT

// Install-path default injected by src/meson.build (lib objects only).
#ifndef BBAI_DEFAULT_STYLE
#define BBAI_DEFAULT_STYLE ""
#endif

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

  Server::Server(bool hl, std::string rc_path)
    : headless(hl), rc_path_(std::move(rc_path)) {
    // Headless treats the install-prefix default style as absent - a box
    // where the product IS installed must not leak prefix state into the
    // golden suite (same stance as rc discovery below). Tests pin a fake
    // default via setDefaultStyleForTest to exercise the middle rung. Set
    // before Config::load so the very first loadStyleWithFallback sees it.
    if (!headless) default_style_path_ = BBAI_DEFAULT_STYLE;
    // Config + style come first - everything below (outputs, toolbar, views)
    // renders through style_. Headless never discovers ~/.blackboxrc on its
    // own: tests must opt into an rc explicitly or a dev box's real config
    // would leak into the golden suite.
    if (rc_path_.empty() && !headless)
      if (const char *home = getenv("HOME"))
        rc_path_ = std::string(home) + "/.blackboxrc";
    config_ = bbai::Config::load(rc_path_);
    style_ = loadStyleWithFallback(config_.styleFile);
    menu_file_ = config().menuFile;   // tilde-expanded by Config (rc-style contract)

    // Workspace count/names from the rc. Applied before any output exists so
    // the toolbar's first render already shows the configured name. The shrink
    // loop stays boot-only: no views exist yet, so dropping workspaces is safe
    // here and only here (applyConfig grows but never shrinks).
    while (workspaces_.count() > config_.workspaceCount && workspaces_.count() > 1)
      workspaces_.removeLastWorkspace();
    applyConfig();

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
    idle_notifier_ = wlr_idle_notifier_v1_create(display);

    scene = wlr_scene_create();
    output_layout = wlr_output_layout_create(display);
    scene_layout = wlr_scene_attach_output_layout(scene, output_layout);

    layer_background = wlr_scene_tree_create(&scene->tree);
    layer_bottom     = wlr_scene_tree_create(&scene->tree);
    layer_window     = wlr_scene_tree_create(&scene->tree);
    layer_top        = wlr_scene_tree_create(&scene->tree);
    layer_fullscreen = wlr_scene_tree_create(&scene->tree);   // above top, below overlay
    layer_overlay    = wlr_scene_tree_create(&scene->tree);
    layer_lock       = wlr_scene_tree_create(&scene->tree);

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
    cursor_axis.connect(&cursor->events.axis, [this](void *data) {
      auto *e = static_cast<wlr_pointer_axis_event *>(data);
      onPointerAxis(e->time_msec, e->orientation, e->delta, e->delta_discrete,
                    e->source, e->relative_direction);
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
    autoraise_timer_ = std::make_unique<Timer>(*timer_registry_, autoraise_handler_);

    // SNI tray host. Real backends only: under headless the developer's
    // session bus must stay untouched (claiming org.kde.StatusNotifierWatcher
    // there would fight the real tray).
    if (!headless) {
      sni_host_ = std::make_unique<sni::Host>(loop);
      installSniHostEvents();
    }

    session_lock_ = std::make_unique<SessionLock>(*this, layer_lock);

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
        // Chrome on the (new) primary comes up through applyConfig - the same
        // gate+knobs path reconfigure and the output-death re-home use. It
        // creates the toolbar under its rc gate and the slit unconditionally
        // (classic has no enable knob for the slit; empty = invisible).
        applyConfig();
        // Give the pointer an image from frame one - otherwise it's invisible
        // over our own chrome until the Super+F7 flow happens to latch one.
        // Real-output only: headless asserts byte-exact goldens and has no
        // screen to point at.
        if (!headless)
          wlr_cursor_set_xcursor(cursor, xcursor_mgr, "default");
      }
      // A head lit up mid-lock must be blanked before anything renders on it.
      if (session_lock_) session_lock_->handleNewOutput(o);
    });

    if (const char *sock = wl_display_add_socket_auto(display))
      socket_name = sock;

    // Exec runner for menu actions (spawned children inherit our WAYLAND_DISPLAY).
    // Headless gets a recording non-spawning default: the ctor's !headless
    // gates below are 'don't even try at boot', this is 'no path can fork on
    // a CI box' - reconfigure() re-runs rootCommand unconditionally, and any
    // future action must not depend on every test remembering to install a
    // fake. Tests that assert argv still install their own FakeCommandRunner.
    if (headless)
      default_runner_ = std::make_unique<FakeCommandRunner>();
    else
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

    // rc rootCommand on a real login only (headless/CI must not spawn shells;
    // the wiring is covered by runRootCommandForTest + FakeCommandRunner).
    if (!headless)
      runRootCommand();
  }

  std::shared_ptr<const Style> Server::loadStyleWithFallback(const std::string &path,
                                                             bool *exact_ok) {
    if (exact_ok) *exact_ok = true;
    // Second face of the headless default-hiding (ctor): when the rc names no
    // style, Config itself defaults styleFile to BBAI_DEFAULT_STYLE - refuse
    // that exact path too, or an installed prefix reaches the goldens anyway.
    const bool hidden = headless && !path.empty()
                        && path == std::string(BBAI_DEFAULT_STYLE);
    if (!hidden)
      if (auto s = Style::load(path, config_.rootCommand)) return s;
    if (exact_ok) *exact_ok = false;
    fprintf(stderr, "blackboxai: style '%s' unreadable, falling back\n", path.c_str());
    if (!default_style_path_.empty())
      if (auto s = Style::load(default_style_path_, config_.rootCommand)) return s;
    return Style::builtin(config_.rootCommand);
  }

  void Server::runRootCommand() {
    // The RC file's rootCommand is user-authored - it gets /bin/sh (classic
    // bexec). The STYLE file's rootCommand never reaches here: it was
    // interpreted into the desktop background by Style (locked policy).
    if (config_.rootCommand.empty()) return;
    commandRunner().run({"/bin/sh", "-c", config_.rootCommand});
  }

  void Server::applyConfig() {
    // Workspaces: names always; count grows only. Shrinking with occupied
    // workspaces means re-homing views - wave-2 configmenu's problem, and
    // classic didn't shrink on reconfigure either.
    while (workspaces_.count() < config_.workspaceCount)
      workspaces_.addWorkspace();
    for (unsigned i = 0; i < config_.workspaceNames.size() && i < workspaces_.count(); ++i)
      workspaces_.setName(i, config_.workspaceNames[i]);

    if (!config_.toolbar.enabled) {
      toolbar_.reset();
    } else if (!toolbar_ && active_output) {
      wlr_output *out = active_output->wlrOutput();
      toolbar_ = std::make_unique<Toolbar>(*this, out->width, out->height);
    }
    if (toolbar_) {
      toolbar_->setPlacement(config_.toolbar.placement);
      toolbar_->setAutoHide(config_.toolbar.autoHide);
    }

    // Slit: exists whenever a primary output does. Applied AFTER the toolbar
    // knobs so the classic overlap shift reads the bar's final rect (a stale
    // toolbar rect here is a subtle one-frame golden flake).
    if (!slit_ && active_output)
      slit_ = std::make_unique<Slit>(*this, *active_output);
    if (slit_)
      slit_->applyOptions(config_.slit);
  }

  void Server::restyle() {
    closeMenus();   // open menus hold old-style buffers; null-safe
    for (Output *o : outputs_) o->renderBackground();
    for (auto &v : views) v->restyle();
    if (toolbar_) toolbar_->restyle();
    if (slit_) slit_->restyle();   // after the toolbar: repositions against its new rect
  }

  bool Server::reconfigure(const std::string &rc_override) {
    if (!rc_override.empty()) rc_path_ = rc_override;
    config_ = bbai::Config::load(rc_path_);
    menu_file_ = config().menuFile;
    menu_loaded_ = false;             // classic reconfigure re-parses the menu
    bool style_ok = true;
    style_ = loadStyleWithFallback(config_.styleFile, &style_ok);
    applyConfig();
    restyle();
    // rc rootCommand re-runs (classic runs it on every style load). No
    // headless gate here and none needed: the headless default runner cannot
    // spawn (ctor), so this is safe on CI whether or not a test installed
    // its own fake.
    runRootCommand();
    return style_ok;
  }

  bool Server::applyStyleFile(const std::string &path) {
    std::shared_ptr<const Style> s = Style::load(path, config_.rootCommand);
    if (!s) return false;
    style_ = std::move(s);
    config_.styleFile = path;
    restyle();
    // Classic saveStyleFilename: the pick survives a restart. Failure to
    // write is loud-but-nonfatal - the live re-theme already happened.
    if (!rc_path_.empty() && !bbai::updateRcKey(rc_path_, "session.styleFile", path))
      fprintf(stderr, "blackboxai: could not persist styleFile to %s\n", rc_path_.c_str());
    return true;
  }

  void Server::setConfigOption(ConfigOption opt) {
    switch (opt) {
    case ConfigOption::FocusClickToFocus:
      config_.focusModel = FocusModel::ClickToFocus;
      // Classic zeroes the raise flags only at load-parse, never on the runtime
      // menu toggle - keep them in memory so a CTF->Sloppy round-trip restores
      // them. focusModelValue() short-circuits to bare "ClickToFocus" while CTF
      // is active, so the persisted spelling is unchanged.
      break;
    case ConfigOption::FocusSloppy:
      config_.focusModel = FocusModel::SloppyFocus;   // raise flags keep their values
      break;
    case ConfigOption::AutoRaise:   config_.autoRaise  = !config_.autoRaise;  break;
    case ConfigOption::ClickRaise:  config_.clickRaise = !config_.clickRaise; break;
    case ConfigOption::FocusNewWindows:
      config_.focusNewWindows = !config_.focusNewWindows;
      break;
    case ConfigOption::PlacementRowSmart: config_.windowPlacement = WindowPlacement::RowSmart; break;
    case ConfigOption::PlacementColSmart: config_.windowPlacement = WindowPlacement::ColSmart; break;
    case ConfigOption::PlacementCenter:   config_.windowPlacement = WindowPlacement::Center;   break;
    case ConfigOption::PlacementCascade:  config_.windowPlacement = WindowPlacement::Cascade;  break;
    case ConfigOption::ToolbarEnabled:  config_.toolbar.enabled  = !config_.toolbar.enabled;  break;
    case ConfigOption::ToolbarAutoHide: config_.toolbar.autoHide = !config_.toolbar.autoHide; break;
    case ConfigOption::ToolbarPlaceTopLeft:      config_.toolbar.placement = toolbar::Placement::TopLeft;      break;
    case ConfigOption::ToolbarPlaceTopCenter:    config_.toolbar.placement = toolbar::Placement::TopCenter;    break;
    case ConfigOption::ToolbarPlaceTopRight:     config_.toolbar.placement = toolbar::Placement::TopRight;     break;
    case ConfigOption::ToolbarPlaceBottomLeft:   config_.toolbar.placement = toolbar::Placement::BottomLeft;   break;
    case ConfigOption::ToolbarPlaceBottomCenter: config_.toolbar.placement = toolbar::Placement::BottomCenter; break;
    case ConfigOption::ToolbarPlaceBottomRight:  config_.toolbar.placement = toolbar::Placement::BottomRight;  break;
    case ConfigOption::SlitAutoHide: config_.slit.autoHide = !config_.slit.autoHide; break;
    case ConfigOption::SlitDirHorizontal: config_.slit.direction = SlitDirection::Horizontal; break;
    case ConfigOption::SlitDirVertical:   config_.slit.direction = SlitDirection::Vertical;   break;
    case ConfigOption::SlitPlaceTopLeft:      config_.slit.placement = SlitPlacement::TopLeft;      break;
    case ConfigOption::SlitPlaceCenterLeft:   config_.slit.placement = SlitPlacement::CenterLeft;   break;
    case ConfigOption::SlitPlaceBottomLeft:   config_.slit.placement = SlitPlacement::BottomLeft;   break;
    case ConfigOption::SlitPlaceTopCenter:    config_.slit.placement = SlitPlacement::TopCenter;    break;
    case ConfigOption::SlitPlaceBottomCenter: config_.slit.placement = SlitPlacement::BottomCenter; break;
    case ConfigOption::SlitPlaceTopRight:     config_.slit.placement = SlitPlacement::TopRight;     break;
    case ConfigOption::SlitPlaceCenterRight:  config_.slit.placement = SlitPlacement::CenterRight;  break;
    case ConfigOption::SlitPlaceBottomRight:  config_.slit.placement = SlitPlacement::BottomRight;  break;
    }

    std::string key, value;
    switch (opt) {
    case ConfigOption::FocusClickToFocus:
    case ConfigOption::FocusSloppy:
    case ConfigOption::AutoRaise:
    case ConfigOption::ClickRaise:
      key = "session.focusModel";
      value = configmenu::focusModelValue(config_);
      break;
    case ConfigOption::FocusNewWindows:
      key = "session.focusNewWindows";
      value = bt::boolAsString(config_.focusNewWindows);
      break;
    case ConfigOption::PlacementRowSmart:
    case ConfigOption::PlacementColSmart:
    case ConfigOption::PlacementCenter:
    case ConfigOption::PlacementCascade:
      key = "session.windowPlacement";
      value = configmenu::windowPlacementValue(config_.windowPlacement);
      break;
    case ConfigOption::ToolbarEnabled:
      key = "session.screen0.enableToolbar";                  // top-level screen key
      value = bt::boolAsString(config_.toolbar.enabled);
      break;
    case ConfigOption::ToolbarAutoHide:
      key = "session.screen0.toolbar.autoHide";
      value = bt::boolAsString(config_.toolbar.autoHide);
      break;
    case ConfigOption::ToolbarPlaceTopLeft:
    case ConfigOption::ToolbarPlaceTopCenter:
    case ConfigOption::ToolbarPlaceTopRight:
    case ConfigOption::ToolbarPlaceBottomLeft:
    case ConfigOption::ToolbarPlaceBottomCenter:
    case ConfigOption::ToolbarPlaceBottomRight:
      key = "session.screen0.toolbar.placement";
      value = barmenu::toolbarPlacementValue(config_.toolbar.placement);
      break;
    case ConfigOption::SlitAutoHide:
      key = "session.screen0.slit.autoHide";
      value = bt::boolAsString(config_.slit.autoHide);
      break;
    case ConfigOption::SlitDirHorizontal:
    case ConfigOption::SlitDirVertical:
      key = "session.screen0.slit.direction";
      value = barmenu::slitDirectionValue(config_.slit.direction);
      break;
    case ConfigOption::SlitPlaceTopLeft:
    case ConfigOption::SlitPlaceCenterLeft:
    case ConfigOption::SlitPlaceBottomLeft:
    case ConfigOption::SlitPlaceTopCenter:
    case ConfigOption::SlitPlaceBottomCenter:
    case ConfigOption::SlitPlaceTopRight:
    case ConfigOption::SlitPlaceCenterRight:
    case ConfigOption::SlitPlaceBottomRight:
      key = "session.screen0.slit.placement";
      value = barmenu::slitPlacementValue(config_.slit.placement);
      break;
    }

    // Nothing WE mutate feeds applyConfig today (focus/placement are read
    // live by their consumers) - the call is the seam contract, so menus'
    // Toolbar*/Slit* values apply for free when they extend the switch.
    applyConfig();
    // Loud-but-nonfatal persist, applyStyleFile precedent (Server.cc:380).
    // The !empty guard matters: a headless test Server has no rc path and
    // updateRcKey("") would create a file literally named "".
    if (!rc_path_.empty() && !bbai::updateRcKey(rc_path_, key, value))
      fprintf(stderr, "blackboxai: could not persist %s to %s\n",
              key.c_str(), rc_path_.c_str());
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
    cursor_axis.disconnect();
    keyboards_.clear();       // drops key/modifiers listeners before the backend finish
    if (sni_menu_reset_idle_) {    // drop the deferred reset before the loop dies
      wl_event_source_remove(sni_menu_reset_idle_);
      sni_menu_reset_idle_ = nullptr;
    }
    sni_menu_.reset();        // cancel any in-flight dbusmenu reply before the Host's bus dies
    active_menu_.reset();     // destroys its overlay scene tree
    destroyScreenshotOverlay(); // null-guarded: frees the dim overlay if a drag was live
    views.clear();
    toolbar_.reset();         // destroys its scene tree + clock Timer (registry still alive)
    autoraise_timer_.reset(); // deregisters before the TimerRegistry dies
    slit_.reset();            // scene tree + hide Timer (registry still alive)
    session_lock_.reset();    // its Timer deregisters + listeners drop before the registry/display die
    timer_registry_.reset();  // removes its wl_event_source before the loop dies
    sni_host_.reset();        // removes its wl_event_sources before the loop dies
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
    if (autoraise_pending_ == view) {         // disarm before the View is freed -
      autoraise_pending_ = nullptr;           // else a pending one-shot fires on a
      if (autoraise_timer_) autoraise_timer_->stop();  // dangling handle
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

  void Server::onViewMapped(View *view) {
    // Mid-alt-tab the commit target is focused_view and the MRU is frozen; a
    // map must not hijack either (same rule the onKey modal block enforces on
    // the key path). The new window is already in mru_/stacking from creation
    // and is focusable once the cycle ends.
    if (cycling_) return;
    // Place the window per policy instead of the fixed (160,120) ctor default.
    // Only plain views: a client that mapped straight into fullscreen/maximize
    // (mpv --fs, applied from initial_commit) already owns its geometry.
    if (!view->isFullscreen() && !view->isMaximized()) {
      if (Output *o = outputForView(view)) {
        const frame::FrameMetrics &fm = currentStyle()->frameMetrics();
        std::vector<wlr_box> taken;
        for (auto &up : views) {
          View *o2 = up.get();
          if (o2 == view || !o2->isMapped() || o2->workspace() != view->workspace())
            continue;
          taken.push_back({o2->x(), o2->y(),
                           frame::frameWidth(o2->contentWidth(), fm),
                           frame::frameHeight(o2->contentHeight(), fm)});
        }
        place::Point p = place::place(config_.windowPlacement, o->workArea(),
                                      frame::frameWidth(view->contentWidth(), fm),
                                      frame::frameHeight(view->contentHeight(), fm),
                                      taken, placement_cascade_);
        view->setPosition(p.x, p.y);
      }
    }
    if (config_.focusNewWindows) focusView(view);
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

  void Server::requestRestart(std::vector<std::string> argv_or_empty) {
    restart_requested_ = true;
    restart_argv_ = std::move(argv_or_empty);
    terminate();
  }

  // MERGE-TRAIN STUB: rc-style owns the real bodies (saveStyleFilename + live
  // re-theme / rc+style reload). When rebasing onto a tree where rc-style has
  // landed, DELETE both bodies here and keep the landed ones - signatures
  // match by seam contract, and the test recorders live in activateMenuItem.
  bool Server::applyStyleFile(const std::string &path) {
    std::fprintf(stderr, "blackboxai: [style] %s (no-op until rc-style lands)\n",
                 path.c_str());
    return false;
  }

  bool Server::reconfigure(const std::string &rc_override) {
    (void)rc_override;
    std::fprintf(stderr, "blackboxai: [reconfig] (no-op until rc-style lands)\n");
    return false;
  }

  bool Server::dispatch() {
    wl_event_loop *loop = wl_display_get_event_loop(display);
    wl_display_flush_clients(display);
    return wl_event_loop_dispatch(loop, 0) >= 0;
  }

  void Server::createSniHostForTest() {
    sni_host_ = std::make_unique<sni::Host>(wl_display_get_event_loop(display));
    installSniHostEvents();
  }

  // HostEvents is a single-slot std::function - last setEvents wins, silently.
  // So the SERVER owns the slot at every creation site and fans out; a second
  // consumer joins here, never via its own setEvents call. Forwarding is
  // null-safe both ways: the ctor site runs before slit_ exists, and headless
  // servers have no host until the test lever runs.
  void Server::installSniHostEvents() {
    if (!sni_host_) return;
    auto fwd = [this](const sni::Item &) { if (slit_) slit_->refresh(); };
    sni_host_->setEvents(sni::HostEvents{fwd, fwd, fwd});
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

  Output *Server::outputForWlr(wlr_output *wo) {
    if (!wo) return nullptr;
    for (Output *o : outputs_)
      if (o->wlrOutput() == wo) return o;
    return nullptr;
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
      // on the survivor through applyConfig, the same gate+knobs path the
      // new_output handler uses: a disabled toolbar stays disabled and the
      // rebuilt one keeps its rc placement/autoHide instead of ctor defaults.
      // If no head survives, the next new_output re-creates it (active_output
      // is null again, so the primary branch re-fires).
      toolbar_.reset();
      slit_.reset();   // its Strut points into the dying Output too
      applyConfig();   // workspace half is idempotent (grow-only + name re-set)
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
    static const std::string empty;
    return toolbar_ ? toolbar_->windowTitleForTest() : empty;
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
    if (v->isFullscreen()) {   // borderless: the whole frame is the client
      const int fx = static_cast<int>(lx) - v->x();
      const int fy = static_cast<int>(ly) - v->y();
      return (fx >= 0 && fy >= 0 && fx < v->contentWidth() && fy < v->contentHeight())
                 ? Part::Client : Part::None;
    }
    using namespace frame;
    const FrameMetrics &m = style_->frameMetrics();
    const int fx = static_cast<int>(lx) - v->x();
    const int fy = static_cast<int>(ly) - v->y();
    const int W = v->contentWidth(), H = v->contentHeight();

    if (!v->drawsFrame()) {  // CSD: only the client area, at the View origin
      return (fx >= 0 && fy >= 0 && fx < W && fy < H) ? Part::Client : Part::None;
    }
    auto in = [&](Rect r) { return fx >= r.x && fy >= r.y && fx < r.x + r.w && fy < r.y + r.h; };
    if (fx >= clientX(m) && fy >= clientY(m) && fx < clientX(m) + W && fy < clientY(m) + H)
      return Part::Client;
    if (in(leftGrip(W, H, m)))  return Part::LeftGrip;
    if (in(rightGrip(W, H, m))) return Part::RightGrip;
    if (in(iconifyButton(W, H, m)))  return Part::IconifyButton;
    if (in(maximizeButton(W, H, m))) return Part::MaximizeButton;
    if (in(closeButton(W, H, m)))    return Part::CloseButton;
    if (in(title(W, H, m)))     return Part::Titlebar;  // incl. the label (drag = move)
    return Part::None;
  }

  void Server::focusView(View *v, bool update_mru) {
    // The lock owns the seat: no caller may focus a client while locked - not
    // onViewMapped (focusNewWindows), not removeView's focus handoff. The
    // locked onKey branch forwards keys to the seat's focused surface, so a
    // steal here delivers the locker's keystrokes (the password) to an app;
    // a steal before the lock surface maps blocks its null-focus keyboard
    // grab entirely. Unlock restores focus via handleSessionUnlocked, which
    // runs after locked_ drops.
    if (session_lock_ && session_lock_->locked()) return;
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
    syncFullscreenLayers(v);   // promote v if fullscreen, demote any other fullscreen
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

  // Any input-funnel entry is user activity. Sits ABOVE the locked gate on
  // purpose - typing at the locker must still reset swayidle's timers. Future
  // input surfaces (an axis handler when someone adds one, touch, tablet) must
  // call this too.
  void Server::notifyIdleActivity() {
    ++idle_activity_count_;   // test: proves an input funnel ran even when it discards
    if (idle_notifier_)
      wlr_idle_notifier_v1_notify_activity(idle_notifier_, seat);
  }

  void Server::onPointerMotion(uint32_t time) {
    notifyIdleActivity();
    if (session_lock_ && session_lock_->locked()) return;  // lock owns the seat; pointer discarded
    if (active_menu_) {
      const int x = static_cast<int>(cursor->x), y = static_cast<int>(cursor->y);
      for (Menu *m = liveMenu(); m; m = m->parent()) {
        const int idx = m->itemIndexAtGlobal(x, y);
        if (idx >= 0) {
          m->setActive(idx);
          if (m->item(idx).kind == MenuItem::Kind::Submenu && m->item(idx).selectable())
            m->openSubmenuAt(idx);
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
    if (slit_) slit_->handlePointerMotion(cursor->x, cursor->y);         // same, for the slit
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
      // Focus-follows-mouse (default-on): the pointer entered a client's own
      // surface. Gated to Part::Client (same condition as the pointer-enter
      // below) so hovering our chrome doesn't thrash focus, and to !cycling_ so
      // a stray motion mid-alt-tab can't scramble the frozen ring. Lock / open
      // menu / screenshot / implicit-grab already returned above. focusView is
      // itself lock-guarded, so this is belt-and-suspenders on the lock path.
      if (config_.focusModel == FocusModel::SloppyFocus && !cycling_ && v != focused_view) {
        focusView(v);
        armAutoRaise(v);   // AutoRaise off -> a no-op that just cancels any pending
      }
      wlr_surface *surf = v->toplevel()->base->surface;
      wlr_seat_pointer_notify_enter(seat, surf, sx, sy);
      wlr_seat_pointer_notify_motion(seat, time, sx, sy);
    } else {
      wlr_seat_pointer_notify_clear_focus(seat);
    }
  }

  void Server::onPointerButton(uint32_t time, uint32_t button,
                               wl_pointer_button_state state) {
    notifyIdleActivity();
    if (session_lock_ && session_lock_->locked()) return;  // no client sees buttons under a lock
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
      // Slit item clicks (classic Slit buttons, SNI-flavored). Before the
      // desktop handlers: a right-click on the slit opens the ITEM's menu
      // path, never the root menu. Hidden slit = no items on screen = no
      // routing; the press still dies here as chrome (overDesktop already
      // rejects layer_top nodes, and pointer focus was cleared over chrome,
      // so nothing leaks to clients either way - the release is seat-filtered
      // because its press was never delivered).
      if (slit_ && !slit_->hidden() &&
          slit_->containsGlobal(static_cast<int>(cursor->x), static_cast<int>(cursor->y))) {
        const int lx = static_cast<int>(cursor->x), ly = static_cast<int>(cursor->y);
        const int idx = slit_->itemIndexAtGlobal(lx, ly);
        sni::Host *host = sniHostOrNull();
        if (idx >= 0 && host && host->ok() &&
            static_cast<std::size_t>(idx) < host->items().size()) {
          const sni::Item &it = host->items()[static_cast<std::size_t>(idx)];
          if (button == BTN_LEFT)        host->activate(it, lx, ly);
          else if (button == BTN_MIDDLE) host->secondaryActivate(it, lx, ly);
          else if (button == BTN_RIGHT)  openSniContextMenu(it, lx, ly);
        } else if (button == BTN_RIGHT) {
          openSlitMenu(lx, ly);   // right-click on the frame (no icon) -> Slit menu
        }
        return;   // swallow frame-gap presses too - chrome, not desktop
      }
      // Right-click on the toolbar opens the Toolbar menu (classic gesture).
      if (button == BTN_RIGHT && toolbar_ &&
          toolbar_->containsGlobal(static_cast<int>(cursor->x),
                                   static_cast<int>(cursor->y))) {
        openToolbarMenu(static_cast<int>(cursor->x), static_cast<int>(cursor->y));
        return;
      }
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
        if (config_.clickRaise) raiseView(v);   // sloppy sub-flag; inert under CTF
        if (button == BTN_RIGHT && (part == Part::Titlebar || part == Part::Label)) {
          openWindowMenu(v, static_cast<int>(cursor->x), static_cast<int>(cursor->y));
          return;   // the titlebar right-click was free (only ever reached the seat)
        }
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

  void Server::onPointerAxis(uint32_t time, wl_pointer_axis orientation, double delta,
                             int32_t delta_discrete, wl_pointer_axis_source source,
                             wl_pointer_axis_relative_direction rel) {
    notifyIdleActivity();                                    // above the lock gate, always
    if (session_lock_ && session_lock_->locked()) return;   // lock owns the seat
    if (active_menu_) return;                                // modal: menus don't scroll
    if (cursor_mode == CursorMode::ScreenshotSelect) return;
    if (cycling_) return;                                    // alt-tab owns input

    // Implicit grab: a button held over the focused client keeps ALL pointer
    // delivery on it (motion does the same at :920-926). Checked BEFORE the
    // wheel-region gate so a mid-drag scroll can't be stolen by the desktop/
    // toolbar gesture - it goes to the grabbed surface, full stop.
    if (seat->pointer_state.button_count > 0 && focused_view &&
        seat->pointer_state.focused_surface == focused_view->toplevel()->base->surface) {
      wlr_seat_pointer_notify_axis(seat, time, orientation, delta, delta_discrete,
                                   source, rel);
      return;
    }

    // Classic wheel gestures (buttons 4/5). Vertical scroll up = delta < 0 =
    // next workspace (classic button4, Screen.cc:2058-2063). Toolbar footprint
    // first (its own key), then the bare desktop; each swallows the event so it
    // never doubles as a client scroll.
    if (orientation == WL_POINTER_AXIS_VERTICAL_SCROLL && delta != 0.0) {
      const int cx = static_cast<int>(cursor->x), cy = static_cast<int>(cursor->y);
      const int dir = (delta < 0.0) ? +1 : -1;
      if (toolbar_ && config_.toolbarActionsWithMouseWheel &&
          toolbar_->containsGlobal(cx, cy)) {
        cycleWorkspace(dir);
        return;
      }
      if (config_.changeWorkspaceWithMouseWheel && overDesktop(cursor->x, cursor->y)) {
        cycleWorkspace(dir);
        return;
      }
    }

    // Default: forward to whatever surface currently holds pointer focus
    // (focused-surface-only delivery, so this is a safe no-op with no focus).
    wlr_seat_pointer_notify_axis(seat, time, orientation, delta, delta_discrete,
                                 source, rel);
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

  void Server::toggleFullscreenForTest() {
    if (focused_view) setViewFullscreen(focused_view, !focused_view->isFullscreen());
  }

  void Server::setViewFullscreen(View *v, bool on, Output *on_output) {
    if (!v) return;
    Output *o = on_output ? on_output : outputForView(v);
    if (!o) return;                                  // zero outputs - nowhere to fill
    v->setFullscreen(on, o->fullBox());
    // Exit while the view was maximized: re-apply maximized geometry onto the
    // (possibly different) target's work area - View left that to us.
    if (!on && v->isMaximized()) v->remaximize(o->workArea());
    // A fullscreen view is promoted only while focused (classic: unfocused
    // fullscreen demotes so an alt-tab preview underneath is visible). Enter
    // while focused -> promote now; exit -> back to the window layer. The
    // focus-change hooks (syncFullscreenLayers) keep it in sync afterwards.
    if (on && focused_view == v) {
      wlr_scene_node_reparent(&v->sceneTree()->node, layer_fullscreen);
      raiseView(v);
    } else if (!on) {
      wlr_scene_node_reparent(&v->sceneTree()->node, layer_window);
    }
  }

  void Server::syncFullscreenLayers(View *newly_focused) {
    // Promote the focused view if it's fullscreen; demote every OTHER fullscreen
    // view back to the window layer. Keeps the "only the focused fullscreen sits
    // above the toolbar" invariant across focus swaps, workspace switches and
    // alt-tab previews.
    bool demoted = false;
    for (auto &up : views) {
      View *v = up.get();
      if (!v->isFullscreen()) continue;
      wlr_scene_tree *want = (v == newly_focused) ? layer_fullscreen : layer_window;
      if (v->sceneTree()->node.parent != want) {
        wlr_scene_node_reparent(&v->sceneTree()->node, want);
        if (v == newly_focused) raiseView(v);
        else demoted = true;
      }
    }
    // A demotion reparents the ex-fullscreen view to the TOP of layer_window,
    // above the window that just took focus (and it's still fullscreen-sized, so
    // it fully covers it). Re-raise the focused non-fullscreen window so the one
    // you switched TO isn't left hidden behind the one you switched from.
    if (demoted && newly_focused && !newly_focused->isFullscreen())
      raiseView(newly_focused);
  }

  bool Server::viewLayerIsFullscreenForTest(View *v) const {
    return v && v->sceneTree()->node.parent == layer_fullscreen;
  }

  void Server::requestFullscreen(View *v) {
    const bool want = v->toplevel()->requested.fullscreen;
    // fullscreen_output can name a specific head; read it at handler time (never
    // cache it - wlroots clears it via a private destroy listener on unplug).
    Output *target = nullptr;
    if (wlr_output *wo = v->toplevel()->requested.fullscreen_output)
      target = outputForWlr(wo);
    setViewFullscreen(v, want, target);
  }

  void Server::requestMaximize(View *v) {
    Output *o = outputForView(v);
    if (o) v->setMaximized(v->toplevel()->requested.maximized, o->workArea());
  }

  void Server::requestMinimize(View *v) {
    if (v->toplevel()->requested.minimized && !v->isIconified()) iconifyView(v);
  }

  void Server::snapFocused(uint32_t edge) {
    View *v = focused_view;
    if (!v) return;
    Output *o = outputForView(v);
    if (!o) return;
    // Leave fullscreen/maximize first - snap is a plain geometry state, and its
    // restore rects are those modes' concern, not ours (un-maximize restores
    // premax, THEN snap overwrites the live geometry).
    if (v->isFullscreen()) setViewFullscreen(v, false);
    if (v->isMaximized()) v->setMaximized(false, o->workArea());

    const wlr_box work = o->workArea();
    const frame::FrameMetrics &fm = currentStyle()->frameMetrics();
    const int halfW = work.width / 2;
    const int contentW = halfW - 2 * fm.border;
    const int contentH = work.height - fm.titleHeight - fm.handleHeight;
    const int x = (edge == WLR_EDGE_LEFT) ? work.x : work.x + (work.width - halfW);
    v->resizeTo(x, work.y, contentW, contentH);
    // Advertise the tiled edges so a cooperating client drops its rounded
    // corners / drop shadow on the snapped side (best-effort; ignored otherwise).
    wlr_xdg_toplevel_set_tiled(v->toplevel(), edge | WLR_EDGE_TOP | WLR_EDGE_BOTTOM);
  }

  void Server::moveFocusedToOutput(wlr_direction dir) {
    View *v = focused_view;
    if (!v) return;
    Output *src = outputForView(v);
    if (!src) return;
    const wlr_box sb = src->fullBox();
    wlr_output *dst_wo = wlr_output_layout_adjacent_output(
        output_layout, dir, src->wlrOutput(),
        sb.x + sb.width / 2.0, sb.y + sb.height / 2.0);
    if (!dst_wo) return;                     // no head that way (POC-proven NULL)
    Output *dst = outputForWlr(dst_wo);
    if (!dst || dst == src) return;

    const wlr_box db = dst->fullBox();
    if (v->isFullscreen()) {
      setViewFullscreen(v, false);           // re-apply on the new head's fullBox
      setViewFullscreen(v, true, dst);
      v->offsetPremax(db.x - sb.x, db.y - sb.y);   // un-fullscreen lands on dst
      return;
    }
    if (v->isMaximized()) {
      v->offsetPremax(db.x - sb.x, db.y - sb.y);   // un-maximize lands on dst
      v->remaximize(dst->workArea());
      return;
    }
    // Plain view: preserve the offset within the source head, clamp onto the
    // target so it can't land off-screen on a smaller monitor.
    int nx = db.x + (v->x() - sb.x);
    int ny = db.y + (v->y() - sb.y);
    if (nx > db.x + db.width  - 1) nx = db.x + db.width  - 1;
    if (ny > db.y + db.height - 1) ny = db.y + db.height - 1;
    if (nx < db.x) nx = db.x;
    if (ny < db.y) ny = db.y;
    v->setPosition(nx, ny);
  }

  int Server::frameWidthForTest(View *v) const {
    return frame::frameWidth(v->contentWidth(), style_->frameMetrics());
  }
  int Server::frameHeightForTest(View *v) const {
    return frame::frameHeight(v->contentHeight(), style_->frameMetrics());
  }

  bool Server::isTopmostForTest(View *v) {
    return v && topmostViewOnWorkspace(v->workspace()) == v;
  }

  void Server::armAutoRaise(View *v) {
    autoraise_pending_ = v;
    if (!autoraise_timer_) return;
    autoraise_timer_->stop();
    if (v && config_.autoRaise)
      autoraise_timer_->start(config_.autoRaiseDelay, /*recurring=*/false);
  }

  void Server::onAutoRaiseTimeout() {
    // Only raise if the pending window is still the one under focus - the mouse
    // may have moved on before the delay elapsed.
    if (autoraise_pending_ && autoraise_pending_ == focused_view)
      raiseView(autoraise_pending_);
    autoraise_pending_ = nullptr;
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

  void Server::injectPointerAxisForTest(wl_pointer_axis orientation, double delta,
                                        int32_t delta_discrete) {
    onPointerAxis(nowMsec(), orientation, delta, delta_discrete,
                  WL_POINTER_AXIS_SOURCE_WHEEL,
                  WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
  }

  void Server::lockForTest() {
    if (!session_lock_) return;
    session_lock_->forceLockedForTest();
    handleSessionLocked(/*takeover=*/false);   // park focus + abort modal modes
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
    notifyIdleActivity();
    if (session_lock_ && session_lock_->locked()) {
      // Every key goes to the lock surface - no bindings, no exceptions.
      // Ctrl+Alt+Backspace's Quit is deliberately suppressed: lock means lock,
      // and the wedged-locker escape is the kernel's VT switch, not ours.
      wlr_seat_set_keyboard(seat, kb);
      wlr_seat_keyboard_notify_key(seat, time, keycode, state);
      return;
    }
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
    if (session_lock_ && session_lock_->locked()) {
      wlr_seat_set_keyboard(seat, kb);
      wlr_seat_keyboard_notify_modifiers(seat, &kb->modifiers);
      return;
    }
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
    case Action::ToggleFullscreen:
      if (focused_view) setViewFullscreen(focused_view, !focused_view->isFullscreen());
      break;
    case Action::SnapLeft:     snapFocused(WLR_EDGE_LEFT);  break;
    case Action::SnapRight:    snapFocused(WLR_EDGE_RIGHT); break;
    case Action::MoveToOutput: moveFocusedToOutput(static_cast<wlr_direction>(a.arg)); break;
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
    syncFullscreenLayers(nullptr);   // nothing focused -> demote every fullscreen view
  }

  void Server::handleSessionLocked(bool takeover) {
    // Abort every modal mode via its canonical cancel: their exit paths
    // re-sync the seat, and locked_ is already true (SessionLock sets it
    // before this hook), so those re-syncs hit the gate instead of handing
    // focus back to a client. cancelCycle's focus-restore does touch a client
    // for an instant - the clearFocus below parks it; enter/leave with no keys
    // in between is harmless.
    if (active_menu_) closeMenus();
    if (cursor_mode == CursorMode::ScreenshotSelect) cancelScreenshot();
    if (cycling_) cancelCycle();
    if (cursor_mode != CursorMode::Passthrough) {   // live move/resize grab
      if (cursor_mode == CursorMode::Resize && grabbed_view)
        wlr_xdg_toplevel_set_resizing(grabbed_view->toplevel(), false);
      cursor_mode = CursorMode::Passthrough;
      grabbed_view = nullptr;
      resize_edges = 0;
    }
    // A binding pressed just before the lock must not leak its release to a
    // client after unlock - the release erase in onKey is gated off while
    // locked, so drop the swallow set here.
    swallowed_keycodes_.clear();
    // Same reasoning for a pending titlebar-button press: its terminating
    // release is discarded by the locked pointer gate, so a stale entry would
    // fire the pre-lock action (or swallow a legit client release) on the
    // first release after unlock.
    pressed_button_view_ = nullptr;
    pressed_button_part_ = Part::None;
    if (!takeover) focus_before_lock_ = focused_view;
    clearFocus();
    wlr_seat_pointer_notify_clear_focus(seat);
  }

  void Server::handleSessionUnlocked() {
    // The pre-lock window may have died under the lock - re-validate the
    // handle, else fall back to the topmost survivor (mirrors removeView).
    if (View *v = viewForHandle(focus_before_lock_)) focusView(v);
    else if (View *top = topmostViewOnWorkspace(workspaces_.current())) focusView(top);
    else clearFocus();
    focus_before_lock_ = nullptr;
    // Re-resolve pointer focus + re-sync modifiers, same as every other modal
    // exit (closeMenus / resyncSeatAfterScreenshot).
    onPointerMotion(nowMsec());
    if (wlr_keyboard *kb = wlr_seat_get_keyboard(seat))
      wlr_seat_keyboard_notify_modifiers(seat, &kb->modifiers);
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

  void Server::removeLastWorkspaceAndRehome() {
    const unsigned n = workspaces_.count();
    if (n <= 1) return;                       // model floor: never below 1
    const unsigned dying = n - 1, survivor = n - 2;
    const bool current_on_dying = (workspaces_.current() == dying);
    // Captured BEFORE re-homing: afterwards every tenant claims the survivor.
    View *keep = (focused_view && focused_view->workspace() == dying)
                     ? focused_view : nullptr;

    for (auto &v : views)
      if (v->workspace() == dying) v->setWorkspace(survivor);

    // Gotcha #29: setCurrentWorkspace's restore branch focuses the incoming
    // workspace's REMEMBERED view - point the survivor's memory at the view
    // that actually holds focus first, so the restore lands on it (focusView
    // early-returns) instead of yanking focus to a stale memory.
    if (keep) workspaces_.setFocused(survivor, keep);

    if (current_on_dying) {
      setCurrentWorkspace(survivor);   // full switch: show/hide + focus restore + label
    } else {
      // No switch happened: sync visibility for the re-homed views (hidden
      // unless the survivor IS current). Idempotent for existing tenants.
      for (auto &v : views)
        if (v->workspace() == survivor)
          v->setOnWorkspace(survivor == workspaces_.current());
    }

    workspaces_.removeLastWorkspace();        // pops the dying slot, clamps current_
    if (toolbar_) toolbar_->redrawWorkspaceLabel();
  }

  void Server::injectKeyForTest(xkb_keysym_t sym, uint32_t mods, bool pressed) {
    notifyIdleActivity();
    if (session_lock_ && session_lock_->locked()) return;  // mirror the onKey gate: no bindings
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

  void Server::setMenuFileForTest(const std::string &path) {
    menu_file_ = path;
    menu_loaded_ = false;      // force a re-parse on the next open
    menu_stamps_.clear();
  }

  bool Server::menuFilesChanged() const {
    for (const MenuStamp &s : menu_stamps_) {
      struct stat st;
      if (stat(s.path.c_str(), &st) != 0) return true;          // vanished -> reread (classic)
      if (st.st_ctim.tv_sec != s.ctime_sec || st.st_ctim.tv_nsec != s.ctime_nsec)
        return true;
    }
    return false;
  }

  void Server::loadMenuFile() {
    menu_title_.clear();
    menu_items_.clear();
    menu_stamps_.clear();
    menu_loaded_ = true;
    if (menu_file_[0] == '|') {
      // Pipe menus popen a generator at menu-open - blocks the compositor
      // loop. Locked v1 non-goal; diagnosed, not silent.
      std::fprintf(stderr,
        "blackboxai: menu: pipe menus (|cmd) are not supported - using the built-in menu\n");
      return;
    }
    menuparser::Result r = menuparser::parseFile(menu_file_);
    for (const std::string &d : r.diagnostics)
      std::fprintf(stderr, "blackboxai: menu: %s\n", d.c_str());
    menu_title_ = std::move(r.title);
    menu_items_ = std::move(r.items);
    for (const std::string &p : r.files) {
      struct stat st;
      if (stat(p.c_str(), &st) == 0)
        menu_stamps_.push_back({p, st.st_ctim.tv_sec, st.st_ctim.tv_nsec});
      // A file that fails stat is not recorded - classic saveMenuFilename
      // does the same; if it appears later the parse won't see it until
      // another recorded file changes. Same limit as classic.
    }
  }

  void Server::openRootMenu(double lx, double ly) {
    if (active_menu_ || sni_menu_) return;   // an in-flight dbusmenu fetch counts
    // A live alt-tab session must dissolve before the menu goes modal, or the
    // later modifier release commits the cycle (focus + workspace switch)
    // underneath the open menu. Commit, not cancel: the preview is the real
    // raise+focus, so committing matches what is on screen at the click.
    if (cycling_) commitCycle();
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
    if (!menu_file_.empty() && (!menu_loaded_ || menuFilesChanged()))
      loadMenuFile();
    std::vector<MenuItem> items = menu_file_.empty()
        ? rootmenu::build(workspaces_)
        : rootmenu::buildFromParsed(menu_items_, workspaces_, config_);
    const std::u32string title =
        (!menu_file_.empty() && !menu_title_.empty()) ? menu_title_
                                                      : rootmenu::title();
    active_menu_ = std::make_unique<Menu>(*this, title, std::move(items));
    active_menu_->show(static_cast<int>(lx), static_cast<int>(ly));
    wlr_seat_pointer_notify_clear_focus(seat);   // input is modal while open
  }

  // The openRootMenu preamble, factored for the new gesture openers so we do
  // NOT edit the landed openRootMenu/openIconMenu. One modal mode at a time:
  // a live alt-tab commits (the preview IS the real focus), an in-progress
  // move/resize grab aborts (its terminating release would be swallowed modal).
  void Server::abortGrabsForMenu() {
    if (cycling_) commitCycle();
    if (cursor_mode != CursorMode::Passthrough) {
      if (cursor_mode == CursorMode::Resize && grabbed_view)
        wlr_xdg_toplevel_set_resizing(grabbed_view->toplevel(), false);
      cursor_mode = CursorMode::Passthrough;
      grabbed_view = nullptr;
      resize_edges = 0;
    }
  }

  void Server::openWindowMenu(View *v, int lx, int ly) {
    if (active_menu_ || sni_menu_) return;
    abortGrabsForMenu();
    active_menu_ = std::make_unique<Menu>(*this, std::u32string{},
                                          windowmenu::build(v, workspaces_),
                                          /*show_title=*/false);
    active_menu_->show(lx, ly);
    wlr_seat_pointer_notify_clear_focus(seat);   // modal while open
  }

  void Server::openToolbarMenu(int lx, int ly) {
    if (active_menu_ || sni_menu_ || !toolbar_) return;
    abortGrabsForMenu();
    active_menu_ = std::make_unique<Menu>(*this, bt::decodeUtf8("Toolbar"),
                                          barmenu::buildToolbar(config_.toolbar));
    active_menu_->show(lx, ly);
    wlr_seat_pointer_notify_clear_focus(seat);
  }

  void Server::openSlitMenu(int lx, int ly) {
    if (active_menu_ || sni_menu_) return;
    abortGrabsForMenu();
    active_menu_ = std::make_unique<Menu>(*this, bt::decodeUtf8("Slit"),
                                          barmenu::buildSlit(config_.slit));
    active_menu_->show(lx, ly);
    wlr_seat_pointer_notify_clear_focus(seat);
  }

  void Server::sendViewToWorkspace(View *v, unsigned ws) {
    if (ws >= workspaces_.count() || ws == v->workspace()) return;
    v->setWorkspace(ws);
    v->setOnWorkspace(ws == workspaces_.current());   // hidden unless target is current
    if (!v->visible() && focused_view == v) {          // focus left with the window
      if (View *top = topmostViewOnWorkspace(workspaces_.current())) focusView(top);
      else clearFocus();
    }
  }

  void Server::scheduleSniMenuReset() {
    if (sni_menu_reset_idle_) return;   // already pending
    sni_menu_reset_idle_ = wl_event_loop_add_idle(
        wl_display_get_event_loop(display), &Server::sniMenuResetIdle, this);
  }

  void Server::sniMenuResetIdle(void *data) {
    auto *self = static_cast<Server *>(data);
    self->sni_menu_reset_idle_ = nullptr;   // libwayland removes the idle after it fires
    std::function<void()> fb = std::move(self->sni_menu_fallback_);
    self->sni_menu_fallback_ = {};
    self->sni_menu_.reset();                 // free the finished client's slots first
    if (fb) fb();                            // then the proxy, on a clean non-reentrant bus
  }

  void Server::showDbusMenu(std::vector<MenuItem> items, int lx, int ly) {
    active_menu_ = std::make_unique<Menu>(*this, std::u32string{}, std::move(items),
                                          /*show_title=*/false);
    active_menu_->show(lx, ly);
    wlr_seat_pointer_notify_clear_focus(seat);
  }

  void Server::openSniContextMenu(const sni::Item &item, int lx, int ly) {
    // A menu-only item (menu_path advertised) tries the com.canonical.dbusmenu
    // client; on a GetLayout error OR empty layout it falls back to the SNI
    // ContextMenu proxy (a faithful superset - an item pointing at a dead menu
    // path should still give the user its SNI context menu). A busless boot or
    // a menu-less item goes straight to the proxy.
    if (item.menu_path.empty() || !sni_host_ || !sni_host_->ok() || !sni_host_->bus()) {
      if (sni_host_ && sni_host_->ok()) sni_host_->contextMenu(item, lx, ly);   // proxy
      return;
    }
    if (active_menu_ || sni_menu_) return;
    const sni::Item snap = item;
    sni_menu_ = std::make_unique<SniMenu>(
        sni_host_->bus(), item.service, item.menu_path,
        [this, snap, lx, ly](bool ok, std::vector<MenuItem> items) {
          if (!ok || items.empty()) {                     // no usable dbusmenu -> proxy
            // This lambda runs INSIDE the Host's sd_bus_process drain. Neither
            // the proxy call (Host::callItem re-enters sd_bus_process via its
            // own drain -> -EBUSY -> the Host tears its bus down) nor freeing
            // the client's slots is safe here. Defer BOTH to an event-loop idle.
            sni_menu_fallback_ = [this, snap, lx, ly] {
              if (sni_host_ && sni_host_->ok()) sni_host_->contextMenu(snap, lx, ly);
            };
            scheduleSniMenuReset();
            return;
          }
          showDbusMenu(std::move(items), lx, ly);
        });
    sni_menu_->open();
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
    if (active_menu_ || sni_menu_) return;   // an in-flight dbusmenu fetch counts
    if (cycling_) commitCycle();   // same rule as openRootMenu: one modal mode at a time
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
    // closeMenus runs from input/reconfigure, never from a bus dispatch, so the
    // direct teardown is safe; drop any deferred reset so it can't fire on a
    // future sni_menu_.
    if (sni_menu_reset_idle_) {
      wl_event_source_remove(sni_menu_reset_idle_);
      sni_menu_reset_idle_ = nullptr;
      sni_menu_fallback_ = {};
    }
    sni_menu_.reset();   // cancel any in-flight dbusmenu reply (slots unref'd)
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
    // A dbusmenu leaf fires its Event over the still-live SniMenu bus context
    // before the chain (and sni_menu_) are torn down. sendClicked queues the
    // message on the shared bus and flushes it, so it goes out even as
    // sni_menu_ dies in the closeMenus below.
    if (copy.action == MenuItem::Act::DbusmenuEvent && sni_menu_)
      sni_menu_->sendClicked(static_cast<int>(copy.workspace));
    closeMenus();
    switch (copy.action) {
    case MenuItem::Act::Exec:            commandRunner().run(copy.argv); break;
    case MenuItem::Act::WorkspaceSwitch: setCurrentWorkspace(copy.workspace); break;
    case MenuItem::Act::NewWorkspace:    workspaces_.addWorkspace(); break;
    case MenuItem::Act::RemoveWorkspace: removeLastWorkspaceAndRehome(); break;
    case MenuItem::Act::Exit:            terminate(); break;
    case MenuItem::Act::Restart:         requestRestart({}); break;
    case MenuItem::Act::RestartOther:    requestRestart(copy.argv); break;
    case MenuItem::Act::SetStyle:
      last_style_request_ = copy.argv.empty() ? std::string() : copy.argv[0];
      applyStyleFile(last_style_request_);   // persist + re-theme (rc-style seam)
      break;
    case MenuItem::Act::Reconfigure:
      ++reconfigure_requests_;
      reconfigure();                         // rc + style reload (rc-style seam)
      menu_loaded_ = false;                  // classic reconfigure re-parses the menu too
      break;
    case MenuItem::Act::WorkspacesMenu:      // submenu markers - a Submenu is
    case MenuItem::Act::ConfigMenu:  break;  // never dispatched as a command
    case MenuItem::Act::ConfigOption:    setConfigOption(copy.option); break;
    case MenuItem::Act::Deiconify:
      if (View *v = viewForHandle(copy.target)) deiconifyView(v);
      break;
    case MenuItem::Act::Iconify:
      if (View *v = viewForHandle(copy.target)) iconifyView(v);
      break;
    case MenuItem::Act::MaximizeToggle:
      if (View *v = viewForHandle(copy.target))
        if (Output *o = outputForView(v))
          v->setMaximized(!v->isMaximized(), o->workArea());
      break;
    case MenuItem::Act::Close:
      if (View *v = viewForHandle(copy.target))
        wlr_xdg_toplevel_send_close(v->toplevel());
      break;
    case MenuItem::Act::SendToWorkspace:
      if (View *v = viewForHandle(copy.target)) sendViewToWorkspace(v, copy.workspace);
      break;
    case MenuItem::Act::DbusmenuEvent: break;   // fired before closeMenus (Task 8)
    case MenuItem::Act::None:            break;
    }
  }

  void Server::handleMenuButton(uint32_t, wl_pointer_button_state state) {
    if (state != WL_POINTER_BUTTON_STATE_PRESSED) return;  // activate on press
    const int x = static_cast<int>(cursor->x), y = static_cast<int>(cursor->y);
    for (Menu *m = liveMenu(); m; m = m->parent()) {
      const int idx = m->itemIndexAtGlobal(x, y);
      if (idx >= 0) {
        if (!m->item(idx).selectable()) return;   // disabled row: swallow, keep the chain open
        if (m->item(idx).kind == MenuItem::Kind::Submenu) {
          m->openSubmenuAt(idx);
          return;
        }
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
