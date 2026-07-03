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
    explicit Server(bool headless);
    ~Server();

    bool ok() const {
      return serverStarted(display != nullptr, backend != nullptr, started_);
    }
    void run();        // wl_display_run (blocking)
    void terminate();  // wl_display_terminate
    bool dispatch();   // single non-blocking event-loop iteration (for tests)

    const std::string &socketName() const { return socket_name; }
    void removeView(View *view);

    // Restack a view to the top/bottom of its layer (model + scene).
    void raiseView(View *view);
    void lowerView(View *view);

    // Shared title-text renderer for window-label decorations (M3). Loads the
    // configured font once; under a test's isolated fontconfig it resolves to
    // the bundled font deterministically.
    bt::TextRenderer *titleFont() { return &title_font; }
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

    // --- test-only input injection + hit-test introspection (headless has no
    // real input devices, so tests drive the SAME onPointer* handlers the real
    // cursor events use) ---
    void injectPointerMotionForTest(double lx, double ly);
    void injectPointerButtonForTest(uint32_t button, bool pressed);
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

    bt::Resource style;  // desktop style driving the background texture

  private:
    friend struct Keyboard;
    friend class SessionLock;   // reaches outputs_/seat + the two hooks below
    enum class CursorMode { Passthrough, Move, Resize, ScreenshotSelect };

    // Pointer handlers shared by real cursor events and test injection.
    void onPointerMotion(uint32_t time);
    void onPointerButton(uint32_t time, uint32_t button, wl_pointer_button_state state);
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
    // Session-lock hooks (called by the friend SessionLock).
    void handleSessionLocked();     // park focus + swallow-state; Task 7 adds modal aborts
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

    bool headless = false;
    bool started_ = false;          // wlr_backend_start succeeded (ok() gate)
    std::string socket_name;
    wlr_session *session_ = nullptr;   // libseat/VT session (DRM only; null nested/headless)
    bt::Listener session_active;       // VT-switch active/inactive -> re-render on resume
    bt::Listener new_output;
    bt::Listener new_xdg_toplevel;
    bt::Listener new_toplevel_decoration;
    bt::Listener new_input;
    bt::Listener cursor_motion, cursor_motion_absolute, cursor_button, cursor_frame;
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
    bt::TextRenderer title_font;                // titlebar label font (M3)
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
