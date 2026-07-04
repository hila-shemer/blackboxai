// The compositor core: owns the wl_display, backend/renderer/allocator, the
// wlr_scene and output layout, the five fixed scene layers, and (M2+) the
// client-facing globals (xdg-shell, single-pixel-buffer, subcompositor,
// data-device). Listens for new toplevels and tracks the mapped Views.
#ifndef BLACKBOXAI_SERVER_HH
#define BLACKBOXAI_SERVER_HH

#include "wlr.hpp"
#include "listener.hpp"
#include "Resource.hh"
#include "Text.hh"
#include "Clock.hh"
#include "Timer.hh"
#include "Workspace.hh"
#include "Keybindings.hh"
#include "ServerOk.hh"
#include "StackingList.hh"
#include "Mru.hh"
#include "CommandRunner.hh"
#include "Decoration.hh"   // bbai::Part
#include "Config.hh"
#include "Style.hh"
#include "MenuItem.hh"

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace bbai {

  class Output;
  class View;
  class Toolbar;
  class Menu;
  class SessionLock;
  struct Keyboard;
  namespace sni { class Host; }

  class Server {
  public:
    explicit Server(bool headless, std::string rc_path = {});
    ~Server();

    bool ok() const {
      return serverStarted(display != nullptr, backend != nullptr, started_);
    }
    void run();        // wl_display_run (blocking)
    void terminate();  // wl_display_terminate
    bool dispatch();   // single non-blocking event-loop iteration (for tests)

    const std::string &socketName() const { return socket_name; }
    void removeView(View *view);
    // A View's surface mapped: apply focus policy (classic focusNewWindows).
    void onViewMapped(View *view);

    // An Output's wlr_output fired destroy (hot-unplug / backend teardown).
    // Called by the Output's own destroy handler BEFORE it deletes itself, so
    // re-homing the toolbar (whose strut points into the dying Output) still
    // has a live object to unregister from.
    void onOutputDestroyed(Output *o);

    // Restack a view to the top/bottom of its layer (model + scene).
    void raiseView(View *view);
    void lowerView(View *view);

    // The window-label font, now owned by the Style (the M3-era shared member
    // is gone). Menu/Toolbar read their own style fonts directly.
    bt::TextRenderer *titleFont() { return style_->windowFont(); }

    // --- config/style seams (wave-1 contracts) ---
    const bbai::Config &config() const { return config_; }
    std::shared_ptr<const Style> currentStyle() const { return style_; }
    void runRootCommandForTest() { runRootCommand(); }
    // Re-read the rc (ctor-remembered path unless overridden), rebuild Config,
    // reload the style through the ladder, re-apply live knobs, restyle.
    // False if the requested style file was unreadable (fallback applied).
    // The wave-2 configmenu / menu-wire [reconfig] entrypoint.
    bool reconfigure(const std::string &rc_override = {});
    // Load one style file, restyle live, persist session.styleFile into the
    // rc (classic saveStyleFilename). False (and no change) if unreadable.
    // menu-wire's Act::SetStyle entrypoint.
    bool applyStyleFile(const std::string &path);
    WorkspaceModel &workspaces() { return workspaces_; }

    // Switch to workspace i (model + toolbar label in B3; view show/hide + focus
    // restore added in B5). No-op if i is out of range or already current.
    void setCurrentWorkspace(unsigned i);

    CommandRunner &commandRunner() { return *command_runner_; }
    void setCommandRunnerForTest(CommandRunner *r) { command_runner_ = r; }

    // XDG autostart test levers: override the dirs to scan and drive the run
    // explicitly (the ctor only runs it on the real backend).
    void setAutostartDirsForTest(const std::vector<std::string> &dirs) { autostart_dirs_ = dirs; }
    void runAutostartForTest() { runAutostart(); }

    // Modal root menu (compositor chrome on layer_overlay).
    void openRootMenu(double lx, double ly);
    void openIconMenu(double lx, double ly);
    void openIconMenuForTest();
    std::vector<MenuItem> buildIconMenu();
    void deiconifyView(View *v);
    void closeMenus();
    void activeOutputSize(int &w, int &h) const;
    // Per-output resolution (work-area slice). outputAt maps a layout point to
    // our Output (nullptr from the layout -> active_output, so callers always
    // get the primary as a floor). outputForView resolves by frame center -
    // dynamic lookup, no stored membership, can't go stale.
    Output *outputAt(double lx, double ly);
    Output *outputForView(const View *v);
    void remaximizeViewsOn(Output *o);
    bool menuOpenForTest() const { return active_menu_ != nullptr; }
    bool screenshotActiveForTest() const { return cursor_mode == CursorMode::ScreenshotSelect; }
    bool screenshotOverlayActiveForTest() const { return screenshot_overlay_ != nullptr; }
    SessionLock *sessionLockForTest() const { return session_lock_.get(); }
    wlr_idle_notifier_v1 *idleNotifierForTest() const { return idle_notifier_; }
    wlr_surface *focusedKeyboardSurfaceForTest() const {
      return seat->keyboard_state.focused_surface;
    }
    int activeMenuItemForTest() const;
    Menu *rootMenuForTest() const { return active_menu_.get(); }

    // M5 menu file: the live root menu parses menu_file_ lazily and re-parses
    // when any recorded file's ctime changed (classic checkMenu, stat-on-open;
    // we compare the full timespec, so tests don't race whole seconds). Empty
    // path = the in-code menu. The ForTest name is the independence shim until
    // rc-style's Config::menuFile lands - production wiring is one ctor line.
    void setMenuFileForTest(const std::string &path);

    // rc-style seams (menu SetStyle/[reconfig] route through these). Stub
    // bodies until rc-style lands - see the MERGE-TRAIN STUB note in Server.cc.
    bool applyStyleFile(const std::string &path);
    bool reconfigure(const std::string &rc_override = {});

    // Restart leaves through main.cc: requestRestart stashes the argv (empty =
    // re-exec self) and terminates the loop; main execs after full teardown.
    void requestRestart(std::vector<std::string> argv_or_empty);
    bool restartRequested() const { return restart_requested_; }
    const std::vector<std::string> &restartArgv() const { return restart_argv_; }
    const std::vector<std::string> &pendingRestartForTest() const { return restart_argv_; }
    const std::string &lastStyleRequestForTest() const { return last_style_request_; }
    int reconfigureRequestsForTest() const { return reconfigure_requests_; }

    // --- test-only input injection + hit-test introspection (headless has no
    // real input devices, so tests drive the SAME onPointer* handlers the real
    // cursor events use) ---
    void injectPointerMotionForTest(double lx, double ly);
    void injectPointerButtonForTest(uint32_t button, bool pressed);
    // Mirrors the real onPointerAxis funnel (modal gates + implicit-grab lock);
    // defaults source=WHEEL, rel=IDENTICAL, time=nowMsec (gotcha #17/#29: tests
    // must exercise the SAME funnel, not a shortcut).
    void injectPointerAxisForTest(wl_pointer_axis orientation, double delta,
                                  int32_t delta_discrete);
    // Drive the session into the locked state without a real locker client, then
    // run the same focus-parking hook a real lock does (mirrors the gate that
    // lock_interactions_test drives through a LockTestClient).
    void lockForTest();
    int idleActivityCountForTest() const { return idle_activity_count_; }
    View *viewAtForTest(double lx, double ly);
    Part partAtForTest(double lx, double ly);
    wlr_surface *focusedPointerSurfaceForTest() const;

    // Clock/timer test levers (headless uses a VirtualClock + manual fireDue, so
    // the ticking clock is deterministic). advanceClockForTest advances the
    // virtual wall+monotonic clock and fires any timers that came due.
    void advanceClockForTest(int64_t seconds);
    int64_t wallSecondsForTest() const;
    bt::Clock &clock() { return *clock_; }
    TimerRegistry &timerRegistry() { return *timer_registry_; }

    // The tray's D-Bus half (watcher + host). Constructed on real backends
    // only - a headless Server must not touch the developer's session bus
    // (same stance as runAutostart); tests build it explicitly under a
    // private dbus-run-session bus. Inert-never-fatal either way.
    sni::Host &sniHost() { return *sni_host_; }
    sni::Host *sniHostForTest() const { return sni_host_.get(); }
    void createSniHostForTest();

    // Deviceless key injection: drives the same binding matcher the real onKey
    // funnel uses (the evdev->XKB seam is covered separately by keycode_test).
    void injectKeyForTest(xkb_keysym_t sym, uint32_t mods, bool pressed);
    int lastActionForTest() const { return last_action_.kind; }
    unsigned currentWorkspaceForTest() const { return workspaces_.current(); }
    View *focusedViewForTest() const { return focused_view; }

    // Alt-tab cycle seams: drive the same session state machine the CycleNext/
    // CyclePrev bindings and the onModifiers commit / Escape cancel funnels use.
    // The device-only "starting modifier went up" release is hand-verified (spec
    // §4); everything it calls is reachable here.
    void cycleForTest(int dir) { cycleStep(dir); }
    void commitCycleForTest() { commitCycle(); }
    void cancelCycleForTest() { cancelCycle(); }
    bool cyclingForTest() const { return cycling_; }
    const std::vector<View *> &mruForTest() const { return mru_.snapshot(); }

    // test-only accessors. activeOutput is the primary (first) head; the count
    // covers every lit head (M7).
    Output *activeOutputForTest() const { return active_output; }
    int outputCountForTest() const { return static_cast<int>(outputs_.size()); }
    Output *outputForTest(int i) const { return outputs_[static_cast<size_t>(i)]; }
    void addHeadlessOutputForTest(int w, int h);
    void destroyOutputForTest(int index);   // wlr_output_destroy on outputs_[index]
    Toolbar *toolbarForTest() const { return toolbar_.get(); }
    const std::string &toolbarWindowTitleForTest() const;
    wlr_scene_output *activeSceneOutput() const;     // production accessor
    wlr_scene_output *activeSceneOutputForTest() const { return activeSceneOutput(); }
    const char *seatSelectionMimeForTest() const;
    wlr_data_source *seatSelectionSourceForTest() const { return seat->selection_source; }
    const std::vector<std::unique_ptr<View>> &viewsForTest() const { return views; }

    wl_display *display = nullptr;
    wlr_backend *backend = nullptr;
    wlr_renderer *renderer = nullptr;
    wlr_allocator *allocator = nullptr;
    wlr_scene *scene = nullptr;
    wlr_output_layout *output_layout = nullptr;
    wlr_scene_output_layout *scene_layout = nullptr;

    // client-facing globals
    wlr_xdg_shell *xdg_shell = nullptr;
    wlr_xdg_decoration_manager_v1 *xdg_decoration = nullptr;

    // input
    wlr_seat *seat = nullptr;
    wlr_cursor *cursor = nullptr;
    wlr_xcursor_manager *xcursor_mgr = nullptr;

    // fixed layer order (bottom -> top): background, bottom, window, top, overlay
    wlr_scene_tree *layer_background = nullptr;
    wlr_scene_tree *layer_bottom = nullptr;
    wlr_scene_tree *layer_window = nullptr;
    wlr_scene_tree *layer_top = nullptr;
    wlr_scene_tree *layer_overlay = nullptr;
    // 6th, topmost: session-lock blanks + lock surfaces. Nothing renders above
    // a locked session - screenshot/menu overlays stay on layer_overlay below.
    wlr_scene_tree *layer_lock = nullptr;

  private:
    friend struct Keyboard;
    friend class SessionLock;   // reaches outputs_/seat + the two hooks below
    enum class CursorMode { Passthrough, Move, Resize, ScreenshotSelect };

    // Pointer handlers shared by real cursor events and test injection.
    void onPointerMotion(uint32_t time);
    void onPointerButton(uint32_t time, uint32_t button, wl_pointer_button_state state);
    void onPointerAxis(uint32_t time, wl_pointer_axis orientation, double delta,
                       int32_t delta_discrete, wl_pointer_axis_source source,
                       wl_pointer_axis_relative_direction rel);
    // Keyboard handlers (called by the per-device Keyboard).
    void onKey(wlr_keyboard *kb, uint32_t time, uint32_t keycode, wl_keyboard_key_state state);
    void onModifiers(wlr_keyboard *kb);
    void removeKeyboard(Keyboard *kb);
    bool dispatchBinding(uint32_t mods, xkb_keysym_t sym);  // true if a binding fired
    void executeAction(const Action &a);
    void notifyIdleActivity();   // ext-idle-notify: call at EVERY input-funnel entry
    void cycleWorkspace(int delta);
    // menu modal helpers
    void handleMenuButton(uint32_t button, wl_pointer_button_state state);
    bool handleMenuKey(xkb_keysym_t sym);                   // true if consumed
    void itemClicked(int index);
    Menu *liveMenu();                                        // deepest open menu in the chain
    void activateMenuItem(const MenuItem &it);               // dispatch + dismiss whole chain
    bool overDesktop(double lx, double ly);                 // background, not a view/chrome
    void beginInteractive(View *v, CursorMode mode, uint32_t edges);
    void beginScreenshot();   // arm region-select mode (crosshair); aborts any grab
    void cancelScreenshot();           // tear down, restore cursor, no capture
    void resyncSeatAfterScreenshot();  // re-resolve pointer focus + modifiers on exit
    void updateScreenshotOverlay();    // reposition the four dim rects to the drag
    void destroyScreenshotOverlay();   // destroy the overlay tree
    void finishScreenshot();           // capture on a real release -> clipboard (T7)
    void processMove();
    void processResize();
    void focusView(View *v, bool update_mru = true);
    void clearFocus();                              // deactivate + clear keyboard focus
    // Session-lock hooks (called by the friend SessionLock). takeover = a new
    // locker replacing a crashed one while locked_ never dropped: everything
    // re-runs except the focus_before_lock_ capture (focused_view is already
    // parked null - re-capturing would lose the unlock restore target).
    void handleSessionLocked(bool takeover);   // park focus + swallow-state + modal aborts
    void handleSessionUnlocked();   // Task 6: restore focus + re-sync the seat
    // Alt-tab MRU cycle (spec §3.2). cycleStep starts or advances the modal
    // session; commit/cancel end it. visibleRing is the frozen candidate set:
    // mapped, non-iconified, across all workspaces, in MRU order.
    void cycleStep(int dir);
    void commitCycle();
    void cancelCycle();
    std::vector<View *> visibleRing() const;
    View *viewForHandle(void *handle);              // a live View matching the stored focus handle
    View *topmostViewOnWorkspace(unsigned ws);
    View *viewFromNode(wlr_scene_node *node);
    Part partAt(View *v, double lx, double ly);
    uint32_t nowMsec() { return next_time++; }

    std::shared_ptr<const Style> loadStyleWithFallback(const std::string &path,
                                                       bool *exact_ok = nullptr);
    void runRootCommand();   // rc-file rootCommand via /bin/sh (user-authored)
    void applyConfig();   // live knobs: toolbar enable/placement/autoHide, workspaces (grow-only)
    void restyle();       // repaint everything off the current style_
    std::string rc_path_;    // remembered for reconfigure()/applyStyleFile()
    bbai::Config config_;
    std::shared_ptr<const Style> style_;

    bool headless = false;
    bool started_ = false;          // wlr_backend_start succeeded (ok() gate)
    bool tearing_down_ = false;     // ~Server: skip toolbar re-home on output death
    std::string socket_name;
    wlr_session *session_ = nullptr;   // libseat/VT session (DRM only; null nested/headless)
    bt::Listener session_active;       // VT-switch active/inactive -> re-render on resume
    bt::Listener new_output;
    bt::Listener new_xdg_toplevel;
    bt::Listener new_toplevel_decoration;
    bt::Listener new_input;
    bt::Listener cursor_motion, cursor_motion_absolute, cursor_button, cursor_frame, cursor_axis;
    Output *active_output = nullptr;            // the primary (first) head: toolbar + work-area
    std::vector<Output *> outputs_;             // every lit head (M7); each self-deletes on its output's destroy
    std::vector<std::unique_ptr<View>> views;   // mapped client windows
    StackingList stacking_;                     // Z-order across all views (M4)
    Mru<View> mru_;                             // last-used order for alt-tab (spec §3.1)

    // Alt-tab cycle session — modal while the starting modifier is held (spec §3.2).
    bool cycling_ = false;
    std::vector<View *> cycle_ring_;            // frozen candidate ring, MRU order
    std::size_t cycle_index_ = 0;               // current position in cycle_ring_
    View *cycle_start_ = nullptr;               // focus to restore on cancel
    uint32_t cycle_mod_ = 0;                    // raw held modifier bit that opened the session
    std::unique_ptr<bt::Clock> clock_;          // wall/monotonic time (M4)
    std::unique_ptr<TimerRegistry> timer_registry_;
    std::unique_ptr<sni::Host> sni_host_;       // tray D-Bus half (sni-core)
    WorkspaceModel workspaces_;                 // 4 default workspaces (M4)
    std::unique_ptr<Toolbar> toolbar_;          // top-layer chrome (M4)
    std::unique_ptr<SessionLock> session_lock_;   // ext-session-lock-v1 (lock-idle)
    wlr_idle_notifier_v1 *idle_notifier_ = nullptr;  // ext-idle-notify-v1
    Keybindings keybindings_;                   // M4 built-in keybinding table
    std::unique_ptr<CommandRunner> default_runner_;  // owns the production runner
    CommandRunner *command_runner_ = nullptr;        // -> default or a test fake
    std::vector<std::unique_ptr<Keyboard>> keyboards_;
    std::set<uint32_t> swallowed_keycodes_;     // bound presses whose release we also swallow
    Action last_action_;                        // last fired binding (test introspection)
    std::unique_ptr<Menu> active_menu_;         // open root menu (nullptr = none); the modal gate

    // menu-file source state (M5). Stamps cover the main file + [include]s +
    // stylesdirs, from menuparser::Result::files.
    struct MenuStamp { std::string path; long ctime_sec; long ctime_nsec; };
    std::string menu_file_;                 // empty -> in-code menu
    bool menu_loaded_ = false;
    std::u32string menu_title_;
    std::vector<MenuItem> menu_items_;
    std::vector<MenuStamp> menu_stamps_;
    void loadMenuFile();                    // parse + stamps + stderr diagnostics
    bool menuFilesChanged() const;          // classic checkMenu (stat-on-open)
    bool restart_requested_ = false;
    std::vector<std::string> restart_argv_;   // empty = re-exec self
    std::string last_style_request_;          // dispatch-side recorder (survives the stub swap)
    int reconfigure_requests_ = 0;

    // interactive grab state
    CursorMode cursor_mode = CursorMode::Passthrough;
    View *grabbed_view = nullptr;
    View *focused_view = nullptr;
    void *focus_before_lock_ = nullptr;   // handle; re-validated on unlock
    double grab_x = 0, grab_y = 0;              // cursor layout pos at grab start
    int grab_geo_x = 0, grab_geo_y = 0;         // view top-left at grab start
    int grab_geo_w = 0, grab_geo_h = 0;         // content size at grab start
    uint32_t resize_edges = 0;                  // wlr_edges bitmask
    uint32_t next_time = 1;                      // monotonic event time seam
    int idle_activity_count_ = 0;                // test: every input-funnel entry bumps this

    // ScreenshotSelect drag state + GNOME-dim overlay (under layer_overlay).
    bool screenshot_dragging_ = false;
    int  screenshot_ax_ = 0, screenshot_ay_ = 0;       // anchor corner A
    wlr_scene_tree *screenshot_overlay_ = nullptr;
    wlr_scene_rect *screenshot_dim_[4] = { nullptr, nullptr, nullptr, nullptr };

    // title-bar button press state: track which view+button was pressed so we
    // can dispatch on release-inside and ignore release-outside (F4.3+).
    Part pressed_button_part_ = Part::None;
    View *pressed_button_view_ = nullptr;
    std::vector<View *> icons_;                 // iconified windows (for icon menu, F4.7)

    void iconifyView(View *v);
    void dispatchButtonRelease(View *v, Part part);

    // XDG autostart: scan autostart_dirs_ (or the default user/system pair),
    // filter via Autostart.hh, and spawn survivors through commandRunner().
    void runAutostart();
    std::vector<std::string> autostart_dirs_;   // test override; empty -> defaults
  };

} // namespace bbai

#endif // BLACKBOXAI_SERVER_HH
