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
#include "StackingList.hh"
#include "CommandRunner.hh"
#include "Decoration.hh"   // bbai::Part

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace bbai {

  class Output;
  class View;
  class Toolbar;
  class Menu;
  struct Keyboard;

  class Server {
  public:
    explicit Server(bool headless);
    ~Server();

    bool ok() const { return display != nullptr && backend != nullptr; }
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

    // Modal root menu (compositor chrome on layer_overlay).
    void openRootMenu(double lx, double ly);
    void closeMenus();
    void activeOutputSize(int &w, int &h) const;
    bool menuOpenForTest() const { return active_menu_ != nullptr; }
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

    // Deviceless key injection: drives the same binding matcher the real onKey
    // funnel uses (the evdev->XKB seam is covered separately by keycode_test).
    void injectKeyForTest(xkb_keysym_t sym, uint32_t mods, bool pressed);
    int lastActionForTest() const { return last_action_.kind; }
    unsigned currentWorkspaceForTest() const { return workspaces_.current(); }
    View *focusedViewForTest() const { return focused_view; }

    // test-only accessors (M1 has a single output)
    Output *activeOutputForTest() const { return active_output; }
    wlr_scene_output *activeSceneOutputForTest() const;
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

    bt::Resource style;  // desktop style driving the background texture

  private:
    friend struct Keyboard;
    enum class CursorMode { Passthrough, Move, Resize };

    // Pointer handlers shared by real cursor events and test injection.
    void onPointerMotion(uint32_t time);
    void onPointerButton(uint32_t time, uint32_t button, wl_pointer_button_state state);
    // Keyboard handlers (called by the per-device Keyboard).
    void onKey(wlr_keyboard *kb, uint32_t time, uint32_t keycode, wl_keyboard_key_state state);
    void onModifiers(wlr_keyboard *kb);
    void removeKeyboard(Keyboard *kb);
    bool dispatchBinding(uint32_t mods, xkb_keysym_t sym);  // true if a binding fired
    void executeAction(const Action &a);
    void cycleWorkspace(int delta);
    // menu modal helpers
    void handleMenuButton(uint32_t button, wl_pointer_button_state state);
    bool handleMenuKey(xkb_keysym_t sym);                   // true if consumed
    void itemClicked(int index);
    bool overDesktop(double lx, double ly);                 // background, not a view/chrome
    void beginInteractive(View *v, CursorMode mode, uint32_t edges);
    void processMove();
    void processResize();
    void focusView(View *v);
    void clearFocus();                              // deactivate + clear keyboard focus
    View *viewForHandle(void *handle);              // a live View matching the stored focus handle
    View *topmostViewOnWorkspace(unsigned ws);
    View *viewFromNode(wlr_scene_node *node);
    Part partAt(View *v, double lx, double ly);
    uint32_t nowMsec() { return next_time++; }

    bool headless = false;
    std::string socket_name;
    bt::Listener new_output;
    bt::Listener new_xdg_toplevel;
    bt::Listener new_toplevel_decoration;
    bt::Listener new_input;
    bt::Listener cursor_motion, cursor_motion_absolute, cursor_button, cursor_frame;
    Output *active_output = nullptr;            // M1: single output
    std::vector<std::unique_ptr<View>> views;   // mapped client windows
    StackingList stacking_;                     // Z-order across all views (M4)
    bt::TextRenderer title_font;                // titlebar label font (M3)
    std::unique_ptr<bt::Clock> clock_;          // wall/monotonic time (M4)
    std::unique_ptr<TimerRegistry> timer_registry_;
    WorkspaceModel workspaces_;                 // 4 default workspaces (M4)
    std::unique_ptr<Toolbar> toolbar_;          // top-layer chrome (M4)
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
    double grab_x = 0, grab_y = 0;              // cursor layout pos at grab start
    int grab_geo_x = 0, grab_geo_y = 0;         // view top-left at grab start
    int grab_geo_w = 0, grab_geo_h = 0;         // content size at grab start
    uint32_t resize_edges = 0;                  // wlr_edges bitmask
    uint32_t next_time = 1;                      // monotonic event time seam
  };

} // namespace bbai

#endif // BLACKBOXAI_SERVER_HH
