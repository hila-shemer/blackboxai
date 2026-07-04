# Window-mgmt (wave-2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the compositor into a real daily driver's window manager: forward the scroll wheel that never reached clients, honor the xdg fullscreen/maximize/minimize requests we are currently ignoring (a live protocol violation), add half-screen snap and move-to-output on the keyboard, and land focus-follows-mouse (default-on) plus Cascade/Center/RowSmart placement so windows stop stacking at the fixed (160,120) wart.

**Architecture:** All behavior, no new chrome. A new `onPointerAxis` funnel mirrors the existing `onPointerMotion`/`onPointerButton` gate order (lock → modal → implicit-grab → wheel-region → forward). Fullscreen is a `View` state flag driving the existing chrome-less relayout branch, promoted onto a new `layer_fullscreen` scene tree the Server owns between `layer_top` and `layer_overlay`. Snap and move-to-output are stateless geometry actions off `Output::workArea()`/`fullBox()` and `wlr_output_layout_adjacent_output`. Sloppy focus is one refocus call at the tail of `onPointerMotion` behind the same early-return gates that already guard the client hit-test, with AutoRaise on an injectable `Timer`. This slice lands FIRST in the wave, so it consumes zero wave-2 sibling seams and takes its zero-golden-churn snapshot before anyone else moves a pixel.

**Tech Stack:** C++20, wlroots 0.20 (all wlr types through `toolkit/wlr.hpp`), meson+ninja, doctest, the headless `TestClient` + golden-PNG harness (`tests/harness/`), VirtualClock-driven `Timer`s.

## Global Constraints

- wlroots pinned **0.20** (`meson.build`); every wlr symbol reaches code only through `toolkit/wlr.hpp`. This box has no wlroots — build and test happen in the container only.
- Coverage gate **80%** lines on `toolkit/ + src/` (CI enforces `--fail-under-line=80`); the project runs ~91 — keep it there. Every method this slice adds is headlessly reachable through `injectPointerAxisForTest`, `injectKeyForTest`, or a `*ForTest` wrapper — no fork paths (headless Servers default to a recording `FakeCommandRunner`, `Server.cc:279`, so nothing forks on CI regardless).
- **Golden policy — window-mgmt gets NEW goldens only, ZERO re-bless of any existing golden.** This slice lands first, before slit's toolbar-inclusive frames and before menus' possible margin swap; like wave-1's work-area, the zero-churn claim is only cleanly checkable at this slot. Exactly ONE sanctioned new golden: `tests/golden/v1-fullscreen.png` (Task 6). Any task whose final `git status tests/golden/` shows a *modified* (not added) file is a bug in that task — fix the code, never `BLESS=1` an existing golden. Menus owns the wave's one coordinated re-bless; it is allowed to sweep our new golden later, that is theirs not ours.
- Any test that renders text or derives a click/hit coordinate from font metrics registers with `text_env` (fontconfig isolation, `tests/meson.build:88`, memory gotcha #20) and guards `REQUIRE(server.titleFont()->height() == 18)` before trusting any y that crosses a titlebar. Pure-geometry tests off `workArea()`/`fullBox()`/`barRectForTest()` need only `test_env`.
- Headless Servers cannot fork (wave-1 behavior); irrelevant here (this slice spawns nothing) but inherited.
- Each task ends green through the container gate. `$WT` = your worktree path (kept under `/home/hila/proj` so the single mount covers the main repo its `.git` file points into):

```bash
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" blackboxai-ci:f44 bash -c '
  [ -d build-f44 ] || meson setup build-f44 -Db_coverage=true -Dbuildtype=debug
  ninja -C build-f44 &&
  export XDG_RUNTIME_DIR=$(mktemp -d) && chmod 700 "$XDG_RUNTIME_DIR" &&
  WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-f44 -j 4 <tests>'
```

`--shm-size=1g` is load-bearing (the 64MB default `/dev/shm` SIGBUS-kills the system suite). `<tests>` is a suite/name filter (`unit`, `axis_forward`, …) or empty for the full gate.
- **This slice OWNS `src/Config.{hh,cc}` for wave 2** (the ownership contradiction is resolved below), `src/View.{hh,cc}`, `src/Keybindings.{hh,cc}`, and `tests/harness/TestClient.{hh,cc}`. It AUTHORS `bool Toolbar::containsGlobal(int,int) const` (one accessor, disjoint from configmenu's clockText one-liner). It shares `src/Server.{hh,cc}` with all three siblings — every region this plan touches (cursor-listener block, `onPointerAxis`, `executeAction`, the ctor scene-layer block, `focusView`/`raiseView`) is called out in the contention map and barely overlaps the menu code.
- Scratch/POCs: `/tmp/claude-1000/-home-hila-proj-blackboxai/8b5e93a4-d1a9-4374-9185-6a529349f965/scratchpad/planning-w2-window-mgmt/` — never into the repo.

## Seams (contracts pinned in the wave-2 synthesis — LAW)

Provided by this slice (later stops consume; all names/types verbatim from the synthesis):
- `void View::setFullscreen(bool on, wlr_box full)` + `bool View::isFullscreen() const` — classic one-shared-premax-rect semantics (save iff `!maximized_ && !fullscreen_`; exit → premax-restore in View, remaximize in Server).
- `Action::{ToggleFullscreen, SnapLeft, SnapRight, MoveToOutput}` appended to the `Action::Kind` enum, with default bindings.
- scene tree `layer_fullscreen` created **between `layer_top` and `layer_overlay`** (menus/screenshot overlays stay above fullscreen; `layer_lock` stays topmost).
- `void Server::injectPointerAxisForTest(wl_pointer_axis orientation, double delta, int32_t delta_discrete)` — mirrors the real `onPointerAxis` funnel including the modal gates and the implicit-grab focus lock.
- `bool Toolbar::containsGlobal(int gx, int gy) const` — one-liner wrapping `currentBarRect()` + the shown-footprint rule; window-mgmt is its first consumer (wheel-over-toolbar), menus reuses it for the toolbar right-click gesture. Nobody derives their own toolbar rect math.

Consumed (all landed wave-1, re-verified against the current tree at the cited lines):
- `Output::fullBox()` (`Output.hh:32`, impl `Output.cc:66`) for fullscreen geometry — **never `workArea()`** (the seam comment at `Output.hh:31` says so); `Output::workArea()` for maximize/snap.
- `Server::outputAt`/`outputForView`/`remaximizeViewsOn` (public, `Server.hh:110-112`) for move-to-output source/target resolution.
- `View` premax machinery: `premax_x/y/w/h` (`View.hh:97`), `remaximize(wlr_box)` guard-bypass (`View.cc:116`), `applyMaximizedGeometry` (`View.cc:107`).
- `config()` (`Server.hh:72`), `iconifyView` (private, `Server.cc:1037`), `raiseView` (public, `Server.hh:64`), `TimerRegistry`/`Timer` (`Timer.hh`, the Toolbar `HideTick` pattern).

## Critiques aimed at this slice — addressed

- **Ownership contradiction (Config.\*):** ADOPTED the synthesis resolution, which is the *opposite* of this slice's own scout proposal. configmenu adds zero Config keys ("recommend none", Config.cc NOT-ours), so **window-mgmt is the sole `src/Config.*` editor of wave 2 and lands its two wheel-bool parse lines itself** — no waiting on a sibling. rc-style's wave-1 "Config.cc rc-style only" pin was whole-wave-1; wave 2 re-pins it here.
- **Wheel-bool defaults asserted-but-unverified → VERIFIED, and the seam text is REBUTTED.** The seam contract provisionally wrote `= false` with an explicit "verify classic defaults at implementation and pin". Verified against `reference/blackboxwm/src/BlackboxResource.cc:197-208`: classic ships **BOTH `changeWorkspaceWithMouseWheel` and `toolbarActionsWithMouseWheel` defaulting `true`**. The critique's own directive — "a wrong default silently changes desktop-scroll behavior for every existing rc" — means match classic. **This plan pins both defaults `true`.** The contract's structural law (exact field names, exact classic keys, window-mgmt owns the file) is honored verbatim; only its provisional default value is corrected per its embedded verify-instruction. `shadeWindowWithMouseWheel` is out of scope — we have no shade state (accepted parity gap), so the toolbar window-label wheel-shade region does not exist.
- **WM_CAPABILITIES asserted-but-unverified → NAMED verification task (Task 1) with a POC line,** per the critique. Verified in the container headers while writing this plan (`/usr/include/wlroots-0.20/wlr/types/wlr_xdg_shell.h`): `wlr_xdg_toplevel_configure.fields` is a bitmask and `WLR_XDG_TOPLEVEL_CONFIGURE_WM_CAPABILITIES` is only ever set by an explicit `wlr_xdg_toplevel_set_wm_capabilities` call — wlroots schedules **no** capabilities field by default, so per xdg-shell a client that never receives a `wm_capabilities` event assumes **all** capabilities supported. We now implement maximize/fullscreen/minimize, so the default (advertise-all-by-omission) is correct and needs no code. Task 1 re-runs this check in the container so the conclusion is reproducible, not a plan-time assertion.
- **Missing mid-drag axis test → ADDED, Task 3:** button held on client A, cursor moved over client B (or the desktop), inject axis → A receives, B/workspace-switch does not fire. The implicit-grab focus-lock check sits *before* the wheel-region gate precisely so this holds.
- **Scene-vs-StackingList layer divergence (`raiseView`/`lowerView` are flat within `layer_window`, ignore the model's 5 layers):** documented as a KNOWN, bounded divergence (Task 6). This slice papers over it for the ONE fullscreen layer via focus-keyed reparenting; a general layer-aware scene restack is explicitly NOT this slice and is flagged so the next Above/Below feature owns it.

## Locked decisions binding this plan

- **Sloppy focus is DEFAULT-ON** (user law): when `session.focusModel` is absent, default `SloppyFocus` (no AutoRaise). An rc key always wins. This slice flips both the `Config.hh` field default and the `Config.cc` parse fallback (Task 11) and updates the one wave-1 test that pinned the old ClickToFocus default. configmenu's merge-slot checklist asserts exactly this landed (`Config{}.focusModel == FocusModel::SloppyFocus`).
- **Keys locked:** `Super+F` fullscreen toggle, `Super+Shift+Left/Right` snap, `Super+Ctrl+{Left,Right,Up,Down}` move-to-output. `Super+Left/Right` stays WorkspacePrev/Next (`Keybindings.cc:11-12`) — untouched.
- **onTop stays inert program-wide** (parsed keys, no rows, no layer machinery) — not this slice's surface, noted for consistency.
- **request_move/request_resize are ack-only v1** (they matter only for CSD holdouts driving their own interactive ops): we schedule the mandated configure by *not* leaving the request unhandled, but do not wire them to `beginInteractive`. Documented at the handler site.

## File map

| File | Change |
|---|---|
| `src/Config.hh` / `.cc` | two `bool` wheel fields (default `true`) + parse lines; focus-model default flips to `SloppyFocus` |
| `src/View.hh` / `.cc` | `fullscreen_` state, `setFullscreen`/`isFullscreen`, chrome-less relayout for fullscreen, request-listener plumbing |
| `src/Keybindings.hh` / `.cc` | four new `Action::Kind` values + default bindings |
| `src/Server.hh` / `.cc` | `onPointerAxis` + cursor-axis listener + `injectPointerAxisForTest`; `layer_fullscreen`; `setViewFullscreen` + focus-keyed reparent in `focusView`/`clearFocus`; `snapFocused`; `moveFocusedToOutput`; `executeAction` cases; sloppy-focus refocus + AutoRaise timer + ClickRaise; `requestFullscreen`/`requestMaximize`/`requestMinimize`; `*ForTest` wrappers |
| `src/Toolbar.hh` / `.cc` | `bool containsGlobal(int,int) const` (one accessor) |
| `tests/harness/TestClient.hh` / `.cc` | axis-event counter + accessor; `setFullscreen`/`setMaximized` request senders |
| `tests/unit/config_test.cc` | wheel-bool defaults; focus-model default flip |
| `tests/unit/keybinding_test.cc` | the four new bindings |
| `tests/system/` | NEW: `axis_forward_test.cc`, `wheel_workspace_test.cc`, `fullscreen_test.cc`, `fullscreen_golden_test.cc`, `snap_test.cc`, `move_to_output_test.cc`, `sloppy_focus_test.cc`, `auto_raise_test.cc` |
| `tests/meson.build` | register the eight new system tests |
| `tests/golden/` | +`v1-fullscreen.png` (the ONLY golden change: an add, never a modify) |

---

### Task 1: Container API verification POC (WM_CAPABILITIES + axis + adjacent-output + set_fullscreen)

Resolves the two asserted-but-unverified critiques against this slice as a reproducible container check before any fullscreen/axis code is written. No repo files change — the deliverable is a compiled+run POC and a recorded conclusion.

**Files:**
- Create: `/tmp/claude-1000/-home-hila-proj-blackboxai/8b5e93a4-d1a9-4374-9185-6a529349f965/scratchpad/planning-w2-window-mgmt/api_probe.cc`

**Interfaces:**
- Consumes: nothing (host headers under `/usr/include/wlroots-0.20`).
- Produces: a documented conclusion baked into Task 5/7's comments — *no `wlr_xdg_toplevel_set_wm_capabilities` call is needed*; the axis event struct + `wlr_seat_pointer_notify_axis` arg order + `wlr_output_layout_adjacent_output` edge behavior are confirmed.

- [x] **Step 1: Write the probe**

Create `api_probe.cc` in the scratchpad dir:

```cpp
// Reproduces the plan-time header reads so the WM_CAPABILITIES conclusion is
// not a bare assertion. Compiles against the container's pkg-config wlroots-0.20.
#include <cstdio>
#define WLR_USE_UNSTABLE
extern "C" {
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/edges.h>
}
int main() {
  // (1) Capabilities are an OPT-IN configure field. wlroots never sets the bit
  // unless the compositor calls set_wm_capabilities, so omitting the call
  // leaves clients assuming ALL caps (xdg-shell default). We implement max/
  // fullscreen/minimize, so omission is correct — no code needed.
  uint32_t caps_field = WLR_XDG_TOPLEVEL_CONFIGURE_WM_CAPABILITIES;
  uint32_t fs_cap = WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN;
  printf("caps configure field bit = %u ; fullscreen cap bit = %u\n", caps_field, fs_cap);

  // (2) The axis event payload + notify arg order the funnel will forward verbatim.
  wlr_pointer_axis_event ev{};
  ev.orientation = WL_POINTER_AXIS_VERTICAL_SCROLL;
  ev.delta = -15.0; ev.delta_discrete = -120;
  ev.source = WL_POINTER_AXIS_SOURCE_WHEEL;
  ev.relative_direction = WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL;
  printf("axis payload: orient=%d delta=%.1f discrete=%d source=%d rel=%d\n",
         ev.orientation, ev.delta, ev.delta_discrete, ev.source, ev.relative_direction);

  // (3) set_fullscreen/set_maximized/set_tiled return a configure serial (uint32_t).
  //     Link-only; do not call (no toplevel here).
  auto *fs = &wlr_xdg_toplevel_set_fullscreen;
  auto *tl = &wlr_xdg_toplevel_set_tiled;
  auto *adj = &wlr_output_layout_adjacent_output;
  printf("linked: set_fullscreen=%p set_tiled=%p adjacent=%p LEFT=%d RIGHT=%d\n",
         (void*)fs, (void*)tl, (void*)adj, WLR_DIRECTION_LEFT, WLR_DIRECTION_RIGHT);
  return 0;
}
```

- [x] **Step 2: Compile + run in the container** (compiled as C to sidestep gotcha #25's `[static N]` C++ parse issue; confirmed caps field bit=2, fullscreen cap bit=4, symbols link, LEFT=4 RIGHT=8 — conclusion: no `set_wm_capabilities` call needed)

```bash
docker run --rm -v /tmp/claude-1000:/tmp/claude-1000 blackboxai-ci:f44 bash -c '
  cd /tmp/claude-1000/-home-hila-proj-blackboxai/8b5e93a4-d1a9-4374-9185-6a529349f965/scratchpad/planning-w2-window-mgmt &&
  g++ -std=c++20 api_probe.cc $(pkg-config --cflags --libs wlroots-0.20 wayland-server) -o api_probe &&
  ./api_probe'
```

Expected: it prints the four lines with non-null function pointers, `RIGHT=8 LEFT=4`, and a positive fullscreen cap bit. Conclusion recorded: **no `set_wm_capabilities` call is added anywhere in this slice** — the xdg-shell default (all caps advertised by omission) already matches our now-complete support. If the compile fails, the container image is wrong; stop and re-pull `blackboxai-ci:f44`.

- [x] **Step 3: No commit** (scratchpad-only). Proceed to Task 2.

---

### Task 2: Config — the two mouse-wheel bools (window-mgmt owns Config.\*)

**Files:**
- Modify: `src/Config.hh` (two fields in `struct Config`, after `doubleClickInterval` at line 72)
- Modify: `src/Config.cc` (parse block, after the placement block ~line 126; classic keys)
- Test: `tests/unit/config_test.cc` (append a TEST_CASE)

**Interfaces:**
- Consumes: `bt::Resource::read` (existing), the `screenName`/`screenClass` helpers (`Config.cc:45-50`).
- Produces: `bool Config::changeWorkspaceWithMouseWheel` (default `true`), `bool Config::toolbarActionsWithMouseWheel` (default `true`). Task 4's `onPointerAxis` reads both via `config()`.

- [x] **Step 1: Write the failing test**

Append to `tests/unit/config_test.cc`:

```cpp
TEST_CASE("mouse-wheel bools: classic keys, classic default True") {
  // Verified against reference/blackboxwm/src/BlackboxResource.cc:197-208 -
  // BOTH default true. A wrong default silently changes desktop-scroll for
  // every existing rc, so this is pinned, not guessed.
  Config d = parse("");
  CHECK(d.changeWorkspaceWithMouseWheel == true);
  CHECK(d.toolbarActionsWithMouseWheel == true);

  Config off = parse("session.changeWorkspaceWithMouseWheel: False\n"
                     "session.toolbarActionsWithMouseWheel: False\n");
  CHECK(off.changeWorkspaceWithMouseWheel == false);
  CHECK(off.toolbarActionsWithMouseWheel == false);
}
```

- [x] **Step 2: Run to verify it fails**

Container gate, `<tests>` = `unit`.
Expected: BUILD FAILURE — `no member named 'changeWorkspaceWithMouseWheel' in 'bbai::Config'`.

- [x] **Step 3: Implement**

`src/Config.hh`, in `struct Config` after `int doubleClickInterval = 250;` (line 72):

```cpp
    // Mouse-wheel gestures (classic session.*WithMouseWheel). Both default
    // True to match classic (BlackboxResource.cc:197-208) - an absent rc keeps
    // desktop/toolbar scroll switching workspaces the way it always did.
    // shadeWindowWithMouseWheel is out of scope: no shade state exists.
    bool changeWorkspaceWithMouseWheel = true;
    bool toolbarActionsWithMouseWheel = true;
```

`src/Config.cc`, after the window-placement block (after line 126's `cfg.windowPlacement = WindowPlacement::RowSmart;` closing `else`):

```cpp
    // --- mouse-wheel gestures (global session keys, classic spellings) ---
    cfg.changeWorkspaceWithMouseWheel =
      res.read("session.changeWorkspaceWithMouseWheel",
               "Session.changeWorkspaceWithMouseWheel", true);
    cfg.toolbarActionsWithMouseWheel =
      res.read("session.toolbarActionsWithMouseWheel",
               "Session.toolbarActionsWithMouseWheel", true);
```

- [x] **Step 4: Run to verify it passes**

Container gate, `<tests>` = `unit`. Expected: `unit` OK.

- [x] **Step 5: Commit**

```bash
git add src/Config.hh src/Config.cc tests/unit/config_test.cc
git commit -m "Config: parse the two mouse-wheel gesture bools (classic default True)

window-mgmt owns Config.* this wave (configmenu adds zero keys). Both
default True per BlackboxResource.cc:197-208 - the seam contract's provisional
False was flagged 'verify'; verified, classic is True, so an existing rc keeps
its desktop-scroll behavior. No consumer yet; onPointerAxis reads these next.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01DfRfEGgpiDzryN8MWZDQeb"
```

---

### Task 3: Cursor axis funnel + test injector + TestClient axis counter

The parked defect: `Server.cc:205-221` wires motion/motion_absolute/button/frame but no axis, so scroll never reaches clients. This adds the funnel, the test injector mirroring it, and proves forwarding + the locked-discard + the mid-drag implicit-grab case.

**Files:**
- Modify: `src/Server.hh` (private decl of `onPointerAxis`; public `injectPointerAxisForTest`; the `cursor_axis` Listener member)
- Modify: `src/Server.cc` (listener in the cursor block ~line 219; disconnect in dtor ~line 399; `onPointerAxis` body near `onPointerMotion`; `injectPointerAxisForTest` near `injectPointerButtonForTest` ~line 1053)
- Modify: `tests/harness/TestClient.hh` (`int pointerAxisEvents() const;`)
- Modify: `tests/harness/TestClient.cc` (`pointer_axis` counter + accessor)
- Create: `tests/system/axis_forward_test.cc`
- Modify: `tests/meson.build` (register `axis_forward`)

**Interfaces:**
- Consumes: `wlr_seat_pointer_notify_axis` (7-arg, `wlr_seat.h`), `notifyIdleActivity` (`Server.cc:883`), the modal state (`session_lock_`, `active_menu_`, `cursor_mode`, `cycling_`), `seat->pointer_state`.
- Produces: `void Server::onPointerAxis(uint32_t time, wl_pointer_axis orientation, double delta, int32_t delta_discrete, wl_pointer_axis_source source, wl_pointer_axis_relative_direction rel)`; `void Server::injectPointerAxisForTest(wl_pointer_axis orientation, double delta, int32_t delta_discrete)` (PINNED SEAM); `TestClient::pointerAxisEvents()`. Task 4 extends `onPointerAxis` with the wheel-region gate.

- [x] **Step 1: Extend TestClient with an axis counter**

`tests/harness/TestClient.cc`, in `struct Impl` after `int pointer_buttons = 0;` (line 35):

```cpp
    int pointer_axis = 0;         // count of wl_pointer.axis events received
```

Replace the stub `ptr_axis` (line 123) so it counts:

```cpp
  static void ptr_axis(void *data, wl_pointer *, uint32_t, uint32_t, wl_fixed_t) {
    static_cast<TestClient::Impl *>(data)->pointer_axis++;
  }
```

After `int TestClient::pointerButtonEvents() const { ... }` (line 196):

```cpp
  int TestClient::pointerAxisEvents() const { return impl ? impl->pointer_axis : 0; }
```

`tests/harness/TestClient.hh`, after `int pointerButtonEvents() const;` (line 34):

```cpp
    int pointerAxisEvents() const;    // count of wl_pointer.axis events received
```

- [x] **Step 2: Write the failing system test**

Create `tests/system/axis_forward_test.cc`:

```cpp
// The parked cursor-axis defect: scroll must reach a focused client, be
// discarded under a lock (but still reset idle timers), and - the synthesis-
// flagged case - ride the implicit grab so a mid-drag scroll goes to the
// grabbed surface no matter where the cursor wandered.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
  }
  // Map one SSD client at the default (160,120), 200x150; return it mapped.
  void mapClient(Server &s, test::TestClient &c) {
    REQUIRE(c.ok());
    auto mapped = [&] { const auto &v = s.viewsForTest();
                        return !v.empty() && v.back()->isMapped(); };
    for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); s.dispatch(); c.pump(); }
    REQUIRE(mapped());
    for (int i = 0; i < 30; ++i) { c.flush(); s.dispatch(); c.pump(); }
  }
  void pumpAxis(Server &s, test::TestClient &c) {
    // Vertical scroll up = one notch; delivery is to the seat's focused surface.
    s.injectPointerAxisForTest(WL_POINTER_AXIS_VERTICAL_SCROLL, -15.0, -120);
    for (int i = 0; i < 20; ++i) { c.flush(); s.dispatch(); c.pump(); }
  }
}

TEST_CASE("axis over a focused client reaches it; over the bare desktop does not") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  // ClickToFocus so a stray hover doesn't refocus - this test is about axis,
  // not focus policy (sloppy focus is exercised in its own test).
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  mapClient(server, c);

  // Cursor over the client area (client x[161,360) y[143,293)); focus + pointer.
  server.injectPointerMotionForTest(250, 200);
  for (int i = 0; i < 10; ++i) { c.flush(); server.dispatch(); c.pump(); }
  const int before = c.pointerAxisEvents();
  pumpAxis(server, c);
  CHECK(c.pointerAxisEvents() == before + 1);

  // Cursor over the bare desktop (top-left corner): no client has pointer focus,
  // and (default True) the desktop wheel gate consumes it - either way the
  // client sees nothing new. (The workspace-switch effect is Task 4's test.)
  server.injectPointerMotionForTest(5, 5);
  const int held = c.pointerAxisEvents();
  pumpAxis(server, c);
  CHECK(c.pointerAxisEvents() == held);
}

TEST_CASE("axis while locked is discarded but still reset idle activity") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFF00FF00u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  mapClient(server, c);
  server.injectPointerMotionForTest(250, 200);
  for (int i = 0; i < 10; ++i) { c.flush(); server.dispatch(); c.pump(); }

  server.lockForTest();                 // wave-1 lock hook (see note below)
  const int before = c.pointerAxisEvents();
  server.injectPointerAxisForTest(WL_POINTER_AXIS_VERTICAL_SCROLL, -15.0, -120);
  for (int i = 0; i < 20; ++i) { c.flush(); server.dispatch(); c.pump(); }
  CHECK(c.pointerAxisEvents() == before);         // no client sees axis under a lock
  CHECK(server.idleActivityCountForTest() > 0);   // but idle timers were reset
}

TEST_CASE("mid-drag axis rides the implicit grab (held on A, cursor over desktop -> A)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  mapClient(server, a);

  // Press a button inside A's client area (implicit grab; button_count > 0).
  server.injectPointerMotionForTest(250, 200);
  server.injectPointerButtonForTest(BTN_LEFT, true);
  for (int i = 0; i < 10; ++i) { a.flush(); server.dispatch(); a.pump(); }

  // Wander over the bare desktop and scroll: the grab keeps delivery on A, and
  // the wheel-region gate must be BYPASSED (no workspace switch).
  const unsigned ws0 = server.currentWorkspaceForTest();
  server.injectPointerMotionForTest(5, 5);
  const int before = a.pointerAxisEvents();
  server.injectPointerAxisForTest(WL_POINTER_AXIS_VERTICAL_SCROLL, -15.0, -120);
  for (int i = 0; i < 20; ++i) { a.flush(); server.dispatch(); a.pump(); }
  CHECK(a.pointerAxisEvents() == before + 1);          // A got the scroll
  CHECK(server.currentWorkspaceForTest() == ws0);      // desktop gate skipped
  server.injectPointerButtonForTest(BTN_LEFT, false);
}
```

Register in `tests/meson.build` (after the `client` block, mirroring it):

```meson
axis_forward_exe = executable('axis-forward-test', files('system/axis_forward_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('axis_forward', axis_forward_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)
```

Note on introspection hooks used above: `lockForTest`, `idleActivityCountForTest`, `currentWorkspaceForTest` — check `Server.hh` first; if any is missing, add the trivial accessor in this task (`lockForTest` should route through the existing `SessionLock` friend path used by `lock_interactions_test.cc`; `idleActivityCountForTest` bumps a counter in `notifyIdleActivity`; `currentWorkspaceForTest` returns `workspaces_.current()`). Do NOT invent a new lock mechanism — reuse whatever `lock_interactions_test.cc` already drives.

- [x] **Step 3: Run to verify it fails**

Container gate, `<tests>` = `axis_forward`.
Expected: BUILD FAILURE — `no member named 'injectPointerAxisForTest'`.

- [x] **Step 4: Implement the funnel**

`src/Server.hh`: add the axis listener beside the others (line 296):

```cpp
    bt::Listener cursor_motion, cursor_motion_absolute, cursor_button, cursor_frame, cursor_axis;
```

Add the private handler decl after `onPointerButton` (line 231):

```cpp
    void onPointerAxis(uint32_t time, wl_pointer_axis orientation, double delta,
                       int32_t delta_discrete, wl_pointer_axis_source source,
                       wl_pointer_axis_relative_direction rel);
```

Add the public injector after `injectPointerButtonForTest` (line 144):

```cpp
    // Mirrors the real onPointerAxis funnel (modal gates + implicit-grab lock);
    // defaults source=WHEEL, rel=IDENTICAL, time=nowMsec (gotcha #17/#29: tests
    // must exercise the SAME funnel, not a shortcut).
    void injectPointerAxisForTest(wl_pointer_axis orientation, double delta,
                                  int32_t delta_discrete);
```

`src/Server.cc`, in the cursor block after `cursor_frame` (line 221):

```cpp
    cursor_axis.connect(&cursor->events.axis, [this](void *data) {
      auto *e = static_cast<wlr_pointer_axis_event *>(data);
      onPointerAxis(e->time_msec, e->orientation, e->delta, e->delta_discrete,
                    e->source, e->relative_direction);
    });
```

Disconnect it in the dtor after `cursor_frame.disconnect();` (line 399):

```cpp
    cursor_axis.disconnect();
```

Add `onPointerAxis` right after `onPointerButton` (after line 1019). This task ships everything EXCEPT the wheel-region gate (Task 4 slots that in at the marked point):

```cpp
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

    // [Task 4 inserts the wheel-region workspace gate here.]

    // Default: forward to whatever surface currently holds pointer focus
    // (focused-surface-only delivery, so this is a safe no-op with no focus).
    wlr_seat_pointer_notify_axis(seat, time, orientation, delta, delta_discrete,
                                 source, rel);
  }
```

Add the injector after `injectPointerButtonForTest` (after line 1057):

```cpp
  void Server::injectPointerAxisForTest(wl_pointer_axis orientation, double delta,
                                        int32_t delta_discrete) {
    onPointerAxis(nowMsec(), orientation, delta, delta_discrete,
                  WL_POINTER_AXIS_SOURCE_WHEEL,
                  WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
  }
```

- [x] **Step 5: Run to verify it passes**

Container gate, `<tests>` = `axis_forward`. Expected: 3 cases pass. Then the FULL gate (empty `<tests>`) — nothing else regresses. Confirm `git status tests/golden/` is empty.

- [x] **Step 6: Commit**

```bash
git add src/Server.hh src/Server.cc tests/harness/TestClient.hh tests/harness/TestClient.cc \
        tests/system/axis_forward_test.cc tests/meson.build
git commit -m "Forward the scroll wheel that never reached clients

cursor->events.axis had no listener since M2, so every wheel event died in the
compositor - a real daily-driver bug. The funnel mirrors onPointerMotion's gate
order; the implicit-grab check sits above the (Task 4) wheel-region gate so a
scroll mid-drag rides the grab to the held surface instead of switching a
workspace under the user's hand. Frames were already forwarded (:219).

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01DfRfEGgpiDzryN8MWZDQeb"
```

---

### Task 4: Toolbar::containsGlobal + wheel workspace switching

**Files:**
- Modify: `src/Toolbar.hh` (decl after `currentBarRect()` line 49) + `src/Toolbar.cc` (one-liner)
- Modify: `src/Server.cc` (`onPointerAxis`: the marked wheel-region insertion point)
- Create: `tests/system/wheel_workspace_test.cc`
- Modify: `tests/meson.build` (register `wheel_workspace`, `text_env` — captures nothing but boots the toolbar)

**Interfaces:**
- Consumes: `Toolbar::currentBarRect()` (`Toolbar.hh:49`), `config().changeWorkspaceWithMouseWheel`/`toolbarActionsWithMouseWheel` (Task 2), `overDesktop` (`Server.cc`, private), `cycleWorkspace` (`Server.cc:1186`).
- Produces: `bool Toolbar::containsGlobal(int gx, int gy) const` (PINNED SEAM — menus reuses it). The wheel gate in `onPointerAxis`.

- [x] **Step 1: Write the failing test**

Create `tests/system/wheel_workspace_test.cc`:

```cpp
// Classic wheel gestures: scroll over the bare desktop cycles workspaces
// (session.changeWorkspaceWithMouseWheel), scroll over the toolbar footprint
// cycles too (session.toolbarActionsWithMouseWheel) - both default True. Up =
// next (classic button4, Screen.cc:2058-2063): axis delta < 0 -> +1.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Toolbar.hh"

#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
    REQUIRE(s.titleFont()->height() == 18);          // gotcha #20: bar geometry pinned
  }
  std::string writeRc(const std::string &body) {
    char t[] = "/tmp/bbai-wheel-XXXXXX";
    REQUIRE(mkdtemp(t) != nullptr);
    std::string p = std::string(t) + "/rc";
    std::ofstream(p) << body;
    return p;
  }
  void scrollUp(Server &s)   { s.injectPointerAxisForTest(WL_POINTER_AXIS_VERTICAL_SCROLL, -15.0, -120); }
  void scrollDown(Server &s) { s.injectPointerAxisForTest(WL_POINTER_AXIS_VERTICAL_SCROLL,  15.0,  120); }
}

TEST_CASE("desktop scroll cycles workspaces by default; up=next, down=prev") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  REQUIRE(server.currentWorkspaceForTest() == 0u);

  server.injectPointerMotionForTest(5, 5);          // bare desktop, top-left
  scrollUp(server);
  CHECK(server.currentWorkspaceForTest() == 1u);    // next
  scrollDown(server);
  CHECK(server.currentWorkspaceForTest() == 0u);    // prev (wraps with 4 ws)
}

TEST_CASE("toolbar scroll cycles workspaces (toolbarActions default True)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  Toolbar *tb = server.toolbarForTest();
  REQUIRE(tb != nullptr);
  const toolbar::Rect b = tb->barRectForTest();     // shown footprint, derived not baked
  server.injectPointerMotionForTest(b.x + b.w / 2, b.y + b.h / 2);
  scrollUp(server);
  CHECK(server.currentWorkspaceForTest() == 1u);
}

TEST_CASE("both gates respect False - scroll is inert on the desktop and bar") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  const std::string rc = writeRc("session.changeWorkspaceWithMouseWheel: False\n"
                                 "session.toolbarActionsWithMouseWheel: False\n");
  Server server(/*headless=*/true, rc);
  boot(server);
  server.injectPointerMotionForTest(5, 5);
  scrollUp(server);
  CHECK(server.currentWorkspaceForTest() == 0u);    // desktop gate off
  const toolbar::Rect b = server.toolbarForTest()->barRectForTest();
  server.injectPointerMotionForTest(b.x + b.w / 2, b.y + b.h / 2);
  scrollUp(server);
  CHECK(server.currentWorkspaceForTest() == 0u);    // toolbar gate off
}
```

Register (after `axis_forward`):

```meson
wheel_workspace_exe = executable('wheel-workspace-test', files('system/wheel_workspace_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('wheel_workspace', wheel_workspace_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)
```

- [x] **Step 2: Run to verify it fails**

Container gate, `<tests>` = `wheel_workspace`.
Expected: BUILD FAILURE — `no member named 'containsGlobal'` (once the gate is added), or CHECK failures on workspace count (before the gate). Either red is fine.

- [x] **Step 3: Implement**

`src/Toolbar.hh`, after `toolbar::Rect currentBarRect() const;` (line 49):

```cpp
    // Global-coord hit-test against the SHOWN footprint (an auto-hidden bar's
    // 2px sliver is NOT a wheel/click target - same hot-zone rule as
    // handlePointerMotion, Toolbar.cc:180). Wave-2 seam: window-mgmt's wheel
    // gate + menus' toolbar right-click gesture share this one rect.
    bool containsGlobal(int gx, int gy) const;
```

`src/Toolbar.cc`, next to `currentBarRect()` (after line 90):

```cpp
  bool Toolbar::containsGlobal(int gx, int gy) const {
    const toolbar::Rect b = currentBarRect();
    return gx >= b.x && gx < b.x + b.w && gy >= b.y && gy < b.y + b.h;
  }
```

`src/Server.cc`, replace the `// [Task 4 inserts the wheel-region workspace gate here.]` marker in `onPointerAxis` with:

```cpp
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
```

- [x] **Step 4: Run to verify it passes**

Container gate, `<tests>` = `wheel_workspace`. Expected: 3 cases pass. Full gate green; `git status tests/golden/` empty.

- [x] **Step 5: Commit**

```bash
git add src/Toolbar.hh src/Toolbar.cc src/Server.cc \
        tests/system/wheel_workspace_test.cc tests/meson.build
git commit -m "Wheel over desktop/toolbar cycles workspaces (classic gesture)

Two gates on the classic keys (both default True), toolbar footprint before
desktop so the bar's own key wins where they overlap. Toolbar::containsGlobal
is the pinned seam - one shown-rect hit-test that menus' toolbar right-click
gesture reuses; nobody re-derives bar math.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01DfRfEGgpiDzryN8MWZDQeb"
```

---

### Task 5: View fullscreen state + chrome-less relayout + Server orchestration (no layering yet)

Fullscreen geometry and the one-shared-premax-rect semantics. Layering (covering the toolbar) is Task 6 — here the fullscreen view fills `fullBox` but still renders under `layer_top`; the test asserts geometry, chrome-hidden, and premax restore numerically, not visually.

**Files:**
- Modify: `src/View.hh` (`fullscreen_` member + `setFullscreen`/`isFullscreen`)
- Modify: `src/View.cc` (`setFullscreen`; `relayout` chrome-less-when-fullscreen)
- Modify: `src/Server.cc` (`partAt` fullscreen branch; `setViewFullscreen`; `toggleFullscreenForTest`)
- Modify: `src/Server.hh` (decls)
- Create: `tests/system/fullscreen_test.cc`
- Modify: `tests/meson.build` (register `fullscreen`)

**Interfaces:**
- Consumes: `Output::fullBox()`, `Output::workArea()`, `outputForView`, `View::remaximize` / `premax_*`, `resizeTo` (`View.cc:127`), `wlr_xdg_toplevel_set_fullscreen`.
- Produces: `void View::setFullscreen(bool on, wlr_box full)` + `bool View::isFullscreen() const` (PINNED SEAM); `void Server::setViewFullscreen(View *v, bool on, Output *on_output = nullptr)`; `void Server::toggleFullscreenForTest()`. Task 6 makes `setViewFullscreen` layer-aware; Task 7's request path calls it; Task 8's key wires it.

- [x] **Step 1: Write the failing test**

Create `tests/system/fullscreen_test.cc`:

```cpp
// Fullscreen geometry + classic one-saved-rect semantics. Layer coverage (the
// toolbar must vanish under a fullscreen view) is the golden test's job; here
// we pin numbers: frame fills fullBox, chrome is gone (partAt = Client
// everywhere, no titlebar), and premax restores exactly - including the classic
// maximize<->fullscreen interplay (one shared saved rect).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"
#include "Output.hh"

#include <cstdlib>

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
  }
  View *mapOne(Server &s, test::TestClient &c) {
    REQUIRE(c.ok());
    auto mapped = [&] { const auto &v = s.viewsForTest();
                        return !v.empty() && v.back()->isMapped(); };
    for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); s.dispatch(); c.pump(); }
    REQUIRE(mapped());
    for (int i = 0; i < 30; ++i) { c.flush(); s.dispatch(); c.pump(); }
    return s.viewsForTest().back().get();
  }
  void settle(Server &s, test::TestClient &c) {
    for (int i = 0; i < 30; ++i) { c.flush(); s.dispatch(); c.pump(); }
  }
}

TEST_CASE("fullscreen fills fullBox, hides chrome, restores premax on exit") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  server.focusViewForTest(v);
  const int px = v->x(), py = v->y(), pw = v->contentWidth(), ph = v->contentHeight();
  const wlr_box full = server.activeOutputForTest()->fullBox();

  server.toggleFullscreenForTest();
  settle(server, c);
  CHECK(v->isFullscreen());
  CHECK(v->x() == full.x);
  CHECK(v->y() == full.y);
  CHECK(v->contentWidth() == full.width);
  CHECK(v->contentHeight() == full.height);
  // Chrome gone: every point of the frame hit-tests to the client, and the
  // titlebar centre is NOT a Titlebar grab anymore.
  CHECK(server.partAtForTest(full.x + 5, full.y + 5) == Server::Part::Client);

  server.toggleFullscreenForTest();
  settle(server, c);
  CHECK_FALSE(v->isFullscreen());
  CHECK(v->x() == px);
  CHECK(v->y() == py);
  CHECK(v->contentWidth() == pw);
  CHECK(v->contentHeight() == ph);
}

TEST_CASE("maximize then fullscreen then exit lands on maximized (classic reMaximize)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFF0000FFu, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  server.focusViewForTest(v);
  const int ow = v->contentWidth(), oh = v->contentHeight();
  const wlr_box work = server.activeOutputForTest()->workArea();

  v->setMaximized(true, work);
  settle(server, c);
  const int mx = v->x(), mw = v->contentWidth();

  server.toggleFullscreenForTest();            // premax NOT re-saved (already maximized)
  settle(server, c);
  CHECK(v->isFullscreen());

  server.toggleFullscreenForTest();            // exit -> reMaximize onto work area
  settle(server, c);
  CHECK(v->isMaximized());
  CHECK(v->x() == mx);
  CHECK(v->contentWidth() == mw);

  v->setMaximized(false, work);                // un-maximize -> the ORIGINAL rect
  settle(server, c);
  CHECK(v->contentWidth() == ow);
  CHECK(v->contentHeight() == oh);
}
```

Register (after `wheel_workspace`):

```meson
fullscreen_exe = executable('fullscreen-test', files('system/fullscreen_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('fullscreen', fullscreen_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)
```

If `Server::Part` is not already reachable from tests (it is a nested type used by `partAtForTest`, `Server.hh`), the existing `hittest_test.cc` shows the accessible spelling — match it. `focusViewForTest` may need adding as a public wrapper around `focusView` (check `Server.hh`; `view_focus_test.cc` likely already exposes one — reuse it).

- [x] **Step 2: Run to verify it fails**

Container gate, `<tests>` = `fullscreen`.
Expected: BUILD FAILURE — `no member named 'setFullscreen'` / `toggleFullscreenForTest`.

- [x] **Step 3: Implement View**

`src/View.hh`, after `bool isMaximized() const { return maximized_; }` (line 62):

```cpp
    // Fullscreen: fill `full` (LAYOUT coords, always the output's fullBox -
    // never the work area) with chrome hidden. Shares ONE saved rect with
    // maximize (classic): save iff neither maximized nor already fullscreen.
    // Exit restores premax only when NOT maximized; the Server re-maximizes
    // otherwise (it owns the work area). `full` is unused on exit.
    void setFullscreen(bool on, wlr_box full);
    bool isFullscreen() const { return fullscreen_; }
```

Add the member after `bool maximized_ = false;` (line 96):

```cpp
    bool fullscreen_ = false;   // fills fullBox, chrome hidden; shares premax with maximize
```

`src/View.cc`, add after `remaximize` (line 119):

```cpp
  void View::setFullscreen(bool on, wlr_box full) {
    if (fullscreen_ == on) return;
    if (on) {
      // One shared saved rect: don't clobber a maximize's premax, and don't
      // re-save our own on a redundant enter.
      if (!maximized_ && !fullscreen_) {
        premax_x = pos_x; premax_y = pos_y; premax_w = cw; premax_h = ch;
      }
      fullscreen_ = true;
      wlr_xdg_toplevel_set_fullscreen(xdg_toplevel, true);   // the mandated ack
      resizeTo(full.x, full.y, full.width, full.height);     // content == fullBox
    } else {
      fullscreen_ = false;
      wlr_xdg_toplevel_set_fullscreen(xdg_toplevel, false);
      // Maximized-underneath restore is the Server's job (work area); here we
      // only restore premax for the plain case.
      if (!maximized_) resizeTo(premax_x, premax_y, premax_w, premax_h);
    }
    relayout();   // re-run the frame/chrome branch for the new fullscreen_ state
  }
```

Change `relayout` (line 63) to drop chrome when fullscreen even for SSD windows:

```cpp
  void View::relayout() {
    // Fullscreen hides chrome regardless of the decoration mode (an SSD window
    // goes borderless while fullscreen, like a CSD holdout).
    const bool chrome = draw_frame && !fullscreen_;
    if (chrome) {
      const frame::FrameMetrics &m = server.currentStyle()->frameMetrics();
      wlr_scene_node_set_position(&surface_tree->node, frame::clientX(m), frame::clientY(m));
      deco->rebuild(*server.currentStyle(), cw, ch, xdg_toplevel->title, focused_);
    } else {
      wlr_scene_node_set_position(&surface_tree->node, 0, 0);
      deco->clear();
    }
    laid_w = cw;
    laid_h = ch;
    laid_frame = chrome;   // track the EFFECTIVE chrome state, not raw draw_frame
  }
```

(Note `laid_frame` now tracks effective chrome; the commit-handler re-layout guard at `View.cc:38` compares `draw_frame != laid_frame` — leave that as-is; `setFullscreen` forces a `relayout()` directly, so a fullscreen toggle never depends on the commit-guard, and a genuine CSD-mode flip still triggers it correctly.)

- [x] **Step 4: Implement Server orchestration**

`src/Server.hh`, after `void applyConfig();` (line 280):

```cpp
    // Fullscreen orchestration: geometry via the target Output's fullBox, plus
    // (Task 6) the layer_fullscreen hop. on_output pins a specific head (a
    // client's requested fullscreen_output); null resolves by frame centre.
    void setViewFullscreen(View *v, bool on, Output *on_output = nullptr);
```

Add the test wrapper near the other `*ForTest` accessors (public, ~line 144):

```cpp
    void toggleFullscreenForTest() {
      if (focused_view) setViewFullscreen(focused_view, !focused_view->isFullscreen());
    }
```

`src/Server.cc`, add `setViewFullscreen` after `dispatchButtonRelease`/`iconifyView` (after line 1044). Task 6 extends this with the layer hop at the marked point:

```cpp
  void Server::setViewFullscreen(View *v, bool on, Output *on_output) {
    if (!v) return;
    Output *o = on_output ? on_output : outputForView(v);
    if (!o) return;                                  // zero outputs - nowhere to fill
    v->setFullscreen(on, o->fullBox());
    // Exit while the view was maximized: re-apply maximized geometry onto the
    // (possibly different) target's work area - View left that to us.
    if (!on && v->isMaximized()) v->remaximize(o->workArea());
    // [Task 6 inserts the layer_fullscreen reparent here.]
  }
```

Add the fullscreen branch at the TOP of `partAt` (line 600), so a fullscreen view is Client-only (no titlebar/grip grab — matches classic disabling move/resize while fullscreen):

```cpp
    if (v->isFullscreen()) {   // borderless: the whole frame is the client
      const int fx = static_cast<int>(lx) - v->x();
      const int fy = static_cast<int>(ly) - v->y();
      return (fx >= 0 && fy >= 0 && fx < v->contentWidth() && fy < v->contentHeight())
                 ? Part::Client : Part::None;
    }
```

- [x] **Step 5: Run to verify it passes**

Container gate, `<tests>` = `fullscreen`. Expected: both cases pass. Full gate green; `git status tests/golden/` empty.

- [x] **Step 6: Commit**

```bash
git add src/View.hh src/View.cc src/Server.hh src/Server.cc \
        tests/system/fullscreen_test.cc tests/meson.build
git commit -m "View fullscreen state + one-shared-premax-rect semantics

setFullscreen fills fullBox with chrome hidden (SSD goes borderless like a CSD
holdout); the saved rect is shared with maximize, so maximize->fullscreen->exit
lands back on the maximized geometry (classic reMaximize) and a later
un-maximize still restores the true original. Layering (covering the toolbar) is
the next commit - here the view fills the screen but still renders under
layer_top; the test pins geometry, not pixels.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01DfRfEGgpiDzryN8MWZDQeb"
```

---

### Task 6: layer_fullscreen scene tree + focus-keyed promote/demote (the visual proof)

**Files:**
- Modify: `src/Server.hh` (`layer_fullscreen` member)
- Modify: `src/Server.cc` (create it in the ctor between `layer_top` and `layer_overlay`; reparent in `setViewFullscreen`; demote/promote in `focusView`/`clearFocus`)
- Create: `tests/system/fullscreen_golden_test.cc`
- Modify: `tests/meson.build` (register `fullscreen_golden`, `text_env` — the frame contains the toolbar we must prove is covered)
- Create: `tests/golden/v1-fullscreen.png` (the ONLY sanctioned golden — an ADD)

**Interfaces:**
- Consumes: `wlr_scene_node_reparent` (`wlr_scene.h`), `raiseView`, the layer trees.
- Produces: `wlr_scene_tree *layer_fullscreen` (public, beside the other layers). Documents the KNOWN scene-vs-model layer divergence.

- [x] **Step 1: Write the failing golden + alt-tab test**

Create `tests/system/fullscreen_golden_test.cc`:

```cpp
// The ONE thing only a frame can prove: a focused fullscreen view sits ABOVE
// the toolbar (layer_fullscreen is between layer_top and layer_overlay), so no
// bar pixels show. Also the classic demote rule: an UNFOCUSED fullscreen view
// drops back to layer_window, so an alt-tab preview of a normal window is
// visible over it. Runs under text_env: the reference frame would otherwise
// contain the toolbar's clock/label text (gotcha #20).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"
#include "Output.hh"

#include <cstdlib>

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
    REQUIRE(s.titleFont()->height() == 18);
  }
  View *mapOne(Server &s, test::TestClient &c) {
    REQUIRE(c.ok());
    auto mapped = [&] { const auto &v = s.viewsForTest();
                        return !v.empty() && v.back()->isMapped(); };
    for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); s.dispatch(); c.pump(); }
    REQUIRE(mapped());
    for (int i = 0; i < 30; ++i) { c.flush(); s.dispatch(); c.pump(); }
    return s.viewsForTest().back().get();
  }
}

TEST_CASE("focused fullscreen covers the toolbar (chrome + bar gone)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFF1188FFu, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  server.focusViewForTest(v);
  server.toggleFullscreenForTest();
  for (int i = 0; i < 30; ++i) { c.flush(); server.dispatch(); c.pump(); }

  test::Frame f = test::captureFrame(server);
  REQUIRE(f.w == 1280u);
  REQUIRE(f.h == 720u);
  // The bottom-centre band is where the BottomCenter toolbar lives; under a
  // fullscreen view it must be the client colour, not bar chrome.
  auto pix = [&](int x, int y) { return f.pixels[static_cast<size_t>(y) * f.w + x] & 0x00FFFFFFu; };
  CHECK(pix(640, 710) == 0x1188FFu);
  CHECK(test::compareGolden(f, "tests/golden/v1-fullscreen.png", 2, 40));
}

TEST_CASE("unfocused fullscreen demotes: alt-tab preview of a normal window shows over it") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient fs(server.socketName(), 0xFF1188FFu, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *vfs = mapOne(server, fs);
  test::TestClient nm(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *vnm = mapOne(server, nm);
  vnm->setPosition(160, 120);

  server.focusViewForTest(vfs);
  server.toggleFullscreenForTest();          // vfs fullscreen + focused -> promoted
  for (int i = 0; i < 20; ++i) { fs.flush(); nm.flush(); server.dispatch(); fs.pump(); nm.pump(); }
  CHECK(server.viewLayerIsFullscreenForTest(vfs));

  // Alt-tab to the normal window: preview focuses vnm, so vfs loses focus and
  // demotes to layer_window; the raised preview must be visible above it.
  server.injectKeyForTest(XKB_KEY_Tab, WLR_MODIFIER_ALT, true);
  for (int i = 0; i < 20; ++i) { fs.flush(); nm.flush(); server.dispatch(); fs.pump(); nm.pump(); }
  CHECK_FALSE(server.viewLayerIsFullscreenForTest(vfs));   // demoted on unfocus
  server.injectKeyForTest(XKB_KEY_Alt_L, 0, false);        // commit the cycle
}
```

Register (after `fullscreen`):

```meson
fullscreen_golden_exe = executable('fullscreen-golden-test',
  files('system/fullscreen_golden_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('fullscreen_golden', fullscreen_golden_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)
```

`viewLayerIsFullscreenForTest(View*)` is a new introspection helper (Step 4). It returns whether the view's `frame_tree` currently parents into `layer_fullscreen`.

- [x] **Step 2: Run to verify it fails**

Container gate, `<tests>` = `fullscreen_golden`.
Expected: BUILD FAILURE — `no member named 'layer_fullscreen'` / `viewLayerIsFullscreenForTest`.

- [x] **Step 3: Create the scene layer**

`src/Server.hh`, add the member with the other layers (after `layer_overlay`, line 219 — declare it BEFORE overlay in z-order terms, but as a field its declaration order doesn't set z; the CREATION order does):

```cpp
    // Between top and overlay: a focused fullscreen view is promoted here so it
    // covers the toolbar (layer_top) but stays under menus/screenshot overlays
    // and the lock. KNOWN divergence: raiseView/lowerView restack flat within a
    // node's parent tree and ignore StackingList's 5 model layers - this slice
    // papers over it for THIS one layer via focus-keyed reparenting; a general
    // layer-aware scene restack is a separate feature (the next Above/Below work
    // owns it), NOT this slice.
    wlr_scene_tree *layer_fullscreen = nullptr;
```

`src/Server.cc`, change the layer-creation block (lines 130-132) so `layer_fullscreen` is created AFTER `layer_top` and BEFORE `layer_overlay` (sibling creation order under `scene->tree` sets z-order, lowest first):

```cpp
    layer_top        = wlr_scene_tree_create(&scene->tree);
    layer_fullscreen = wlr_scene_tree_create(&scene->tree);   // above top, below overlay
    layer_overlay    = wlr_scene_tree_create(&scene->tree);
    layer_lock       = wlr_scene_tree_create(&scene->tree);
```

- [x] **Step 4: Reparent on focus change**

In `setViewFullscreen`, replace the `// [Task 6 inserts the layer_fullscreen reparent here.]` marker:

```cpp
    // A fullscreen view is promoted only while focused (classic: unfocused
    // fullscreen demotes so an alt-tab preview underneath is visible). Enter
    // while focused -> promote now; exit -> back to the window layer. The
    // focus-change hooks below keep it in sync afterwards.
    if (on && focused_view == v) {
      wlr_scene_node_reparent(&v->sceneTree()->node, layer_fullscreen);
      raiseView(v);
    } else if (!on) {
      wlr_scene_node_reparent(&v->sceneTree()->node, layer_window);
    }
```

Add a private helper and call it from `focusView` and `clearFocus`. In `focusView` (after the new focus is set, before the toolbar label redraw ~line 645), and in `clearFocus`, keep the fullscreen layer keyed to focus:

```cpp
  void Server::syncFullscreenLayers(View *newly_focused) {
    // Promote the focused view if it's fullscreen; demote every OTHER fullscreen
    // view back to the window layer. Keeps the "only the focused fullscreen sits
    // above the toolbar" invariant across focus swaps, workspace switches and
    // alt-tab previews.
    for (auto &up : views) {
      View *v = up.get();
      if (!v->isFullscreen()) continue;
      wlr_scene_tree *want = (v == newly_focused) ? layer_fullscreen : layer_window;
      if (v->sceneTree()->node.parent != want) {
        wlr_scene_node_reparent(&v->sceneTree()->node, want);
        if (v == newly_focused) raiseView(v);
      }
    }
  }
```

Declare it private in `Server.hh` (near `focusView`). Call `syncFullscreenLayers(v);` at the end of `focusView` and `syncFullscreenLayers(nullptr);` at the end of `clearFocus`. (This subsumes the enter-branch promote in `setViewFullscreen`, but keep both: `setViewFullscreen` handles the case where the view is already focused when it goes fullscreen, and the focus hooks handle later swaps.)

Add the introspection helper (public, near `viewsForTest`):

```cpp
    bool viewLayerIsFullscreenForTest(View *v) const {
      return v && v->sceneTree()->node.parent == layer_fullscreen;
    }
```

- [x] **Step 5: Bless the ONE new golden, then run**

```bash
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" blackboxai-ci:f44 bash -c '
  ninja -C build-f44 &&
  export XDG_RUNTIME_DIR=$(mktemp -d) && chmod 700 "$XDG_RUNTIME_DIR" &&
  BLESS=1 WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-f44 fullscreen_golden &&
  WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-f44 -j 4'
```

Expected: full gate green. Then `git status tests/golden/` must show EXACTLY one line — `tests/golden/v1-fullscreen.png` as an **untracked add** (`??`), never a modify of an existing file. If any existing golden shows modified, STOP: the layer creation shifted a pixel it shouldn't have — investigate, do not bless.

- [x] **Step 6: Commit**

```bash
git add src/Server.hh src/Server.cc tests/system/fullscreen_golden_test.cc \
        tests/meson.build tests/golden/v1-fullscreen.png
git commit -m "Fullscreen views cover the toolbar; unfocused ones demote

New layer_fullscreen between layer_top and layer_overlay; focus-keyed
reparenting promotes the focused fullscreen view over the bar and drops every
other one back to layer_window so an alt-tab preview underneath stays visible
(classic demote-on-unfocus). Papers over the flat scene-vs-StackingList layer
divergence for this ONE layer only - a general layer-aware restack is the next
Above/Below feature's job, flagged at the member. One new golden, zero re-bless.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01DfRfEGgpiDzryN8MWZDQeb"
```

---

### Task 7: xdg request listeners — fix the live protocol violation

Today `src/` has ZERO listeners for `request_maximize`/`request_fullscreen`/`request_minimize`/`request_move`/`request_resize` (verified: `rg` over `src/` = no hits). The header comment above these signals is explicit: not scheduling a configure in response "is a protocol violation" — any client sending `set_fullscreen`/`set_maximized` today gets no configure. The `set_*` calls we already make (Task 5, `setMaximized`) ARE the mandated ack.

**Files:**
- Modify: `src/View.hh` (request Listener members)
- Modify: `src/View.cc` (connect the listeners; re-run pending fullscreen from `initial_commit`)
- Modify: `src/Server.hh` / `.cc` (`requestFullscreen`/`requestMaximize`/`requestMinimize`)
- Modify: `tests/harness/TestClient.hh` / `.cc` (`setFullscreen`/`setMaximized` request senders)
- Create: `tests/system/xdg_request_test.cc`
- Modify: `tests/meson.build` (register `xdg_request`)

**Interfaces:**
- Consumes: `xdg_toplevel->requested` (`{maximized, minimized, fullscreen, fullscreen_output}`), `xdg_toplevel->base->initialized`, `setViewFullscreen` (Task 5/6), `setMaximized`, `iconifyView`, `outputForView`.
- Produces: `void Server::requestFullscreen(View *v)`, `void Server::requestMaximize(View *v)`, `void Server::requestMinimize(View *v)`; `TestClient::setFullscreen(bool)` / `setMaximized(bool)`.

- [ ] **Step 1: Extend TestClient to send the requests**

`tests/harness/TestClient.hh`, after `destroyDecorationForTest();` (line 38):

```cpp
    void setFullscreen(bool on);   // xdg_toplevel.set_fullscreen / unset_fullscreen
    void setMaximized(bool on);    // xdg_toplevel.set_maximized / unset_maximized
```

`tests/harness/TestClient.cc` — implement against the stored `impl->toplevel` (the `xdg_toplevel` proxy; grep the file for its member name and match it):

```cpp
  void TestClient::setFullscreen(bool on) {
    if (!impl || !impl->toplevel) return;
    if (on) xdg_toplevel_set_fullscreen(impl->toplevel, nullptr);
    else    xdg_toplevel_unset_fullscreen(impl->toplevel);
  }
  void TestClient::setMaximized(bool on) {
    if (!impl || !impl->toplevel) return;
    if (on) xdg_toplevel_set_maximized(impl->toplevel);
    else    xdg_toplevel_unset_maximized(impl->toplevel);
  }
```

- [ ] **Step 2: Write the failing test**

Create `tests/system/xdg_request_test.cc`:

```cpp
// The protocol-violation fix: a client that asks for fullscreen/maximize/
// minimize must get a configure and the state applied. Also the mpv case -
// set_fullscreen BEFORE the first commit must not assert (gotcha #13); it
// applies at map from the initial_commit re-run.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"
#include "Output.hh"

#include <cstdlib>

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
  }
  View *mapOne(Server &s, test::TestClient &c) {
    REQUIRE(c.ok());
    auto mapped = [&] { const auto &v = s.viewsForTest();
                        return !v.empty() && v.back()->isMapped(); };
    for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); s.dispatch(); c.pump(); }
    REQUIRE(mapped());
    for (int i = 0; i < 30; ++i) { c.flush(); s.dispatch(); c.pump(); }
    return s.viewsForTest().back().get();
  }
  void settle(Server &s, test::TestClient &c) {
    for (int i = 0; i < 40; ++i) { c.flush(); s.dispatch(); c.pump(); }
  }
}

TEST_CASE("client set_fullscreen is honored (protocol obligation)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFFAABBCCu, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  const wlr_box full = server.activeOutputForTest()->fullBox();

  c.setFullscreen(true);
  settle(server, c);
  CHECK(v->isFullscreen());
  CHECK(v->contentWidth() == full.width);

  c.setFullscreen(false);
  settle(server, c);
  CHECK_FALSE(v->isFullscreen());
}

TEST_CASE("client set_maximized is honored") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFF445566u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  const wlr_box work = server.activeOutputForTest()->workArea();
  c.setMaximized(true);
  settle(server, c);
  CHECK(v->isMaximized());
  CHECK(v->x() == work.x);
}

TEST_CASE("pre-map set_fullscreen (mpv --fs) applies at map, no assert") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  // Ask for fullscreen the instant the toplevel exists, before the first
  // buffer commit - the handler must defer to initial_commit, not schedule a
  // configure on an uninitialized surface.
  test::TestClient c(server.socketName(), 0xFF778899u, 200, 150,
                     test::TestClient::Deco::RequestSSD,
                     /*fullscreen_before_map=*/true);
  View *v = mapOne(server, c);   // must not have asserted in schedule_configure
  settle(server, c);
  CHECK(v->isFullscreen());
}
```

The `fullscreen_before_map` ctor flag is a small `TestClient` addition: after creating the toplevel but before the first `wl_surface_commit`, call `xdg_toplevel_set_fullscreen`. Add the optional bool param (default false) to the `TestClient` ctor and gate the pre-commit request on it. Register (after `fullscreen_golden`):

```meson
xdg_request_exe = executable('xdg-request-test', files('system/xdg_request_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('xdg_request', xdg_request_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)
```

- [ ] **Step 3: Run to verify it fails**

Container gate, `<tests>` = `xdg_request`.
Expected: BUILD FAILURE — `no member named 'setFullscreen'` (TestClient), then behavior failures once that compiles.

- [ ] **Step 4: Implement the listeners**

`src/View.hh`, add members after `deco_request_mode_, deco_destroy_;` (line 106):

```cpp
    bt::Listener req_maximize_, req_fullscreen_, req_minimize_, req_move_, req_resize_;
```

`src/View.cc`, in the ctor after the `destroy_` listener (line 52), connect them. request_move/request_resize are ack-only v1 (the configure obligation is met by wlroots' own request bookkeeping; we deliberately don't drive `beginInteractive` — CSD holdouts are the only clients that send these, and v1 doesn't chase them):

```cpp
    // xdg state requests: the protocol REQUIRES a configure in response even if
    // nothing changed (wlr_xdg_shell.h). We route the three we implement through
    // the Server (it resolves the output); request_move/resize are ack-only v1.
    req_fullscreen_.connect(&xdg_toplevel->events.request_fullscreen, [this](void *) {
      if (xdg_toplevel->base->initialized) server.requestFullscreen(this);
      // else: applied from the initial_commit block below (gotcha #13).
    });
    req_maximize_.connect(&xdg_toplevel->events.request_maximize, [this](void *) {
      if (xdg_toplevel->base->initialized) server.requestMaximize(this);
    });
    req_minimize_.connect(&xdg_toplevel->events.request_minimize, [this](void *) {
      if (xdg_toplevel->base->initialized) server.requestMinimize(this);
    });
    req_move_.connect(&xdg_toplevel->events.request_move, [this](void *) {});    // ack-only v1
    req_resize_.connect(&xdg_toplevel->events.request_resize, [this](void *) {}); // ack-only v1
```

In the `commit_` handler's `initial_commit` block (after `wlr_xdg_toplevel_set_size(...)` at `View.cc:29`), apply any state the client requested before it was initialized:

```cpp
      // A client can request state before the first commit (mpv --fs); the
      // request handlers deferred, so apply from here now that the surface is
      // initialized and set_* can schedule a configure.
      if (xdg_toplevel->requested.fullscreen) server.requestFullscreen(this);
      else if (xdg_toplevel->requested.maximized) server.requestMaximize(this);
      return;
```

(The `map_` handler runs after `initial_commit`; `requestFullscreen` reads `outputForView(this)` which needs the view mapped for a sane frame centre. If `activeOutputForTest`/`outputForView` returns the primary as a floor even pre-map, this is safe; if not, move the deferred apply into the `map_` handler instead — verify against `outputForView`'s null-floor behavior at `Server.cc:498-503`, which returns `active_output` as a floor, so pre-map resolution is safe.)

`src/Server.hh`, after `setViewFullscreen` (line ~281):

```cpp
    void requestFullscreen(View *v);   // client set_fullscreen -> apply + configure
    void requestMaximize(View *v);     // client set_maximized  -> apply + configure
    void requestMinimize(View *v);     // client set_minimized  -> iconify + configure
```

`src/Server.cc`, after `setViewFullscreen`:

```cpp
  void Server::requestFullscreen(View *v) {
    const bool want = v->toplevel()->requested.fullscreen;
    // fullscreen_output can name a specific head; read it at handler time (never
    // cache it - wlroots clears it via a private destroy listener on unplug).
    Output *target = nullptr;
    if (wlr_output *wo = v->toplevel()->requested.fullscreen_output)
      target = outputAt(0, 0), target = nullptr, target = outputForWlr(wo);
    setViewFullscreen(v, want, target);
  }

  void Server::requestMaximize(View *v) {
    Output *o = outputForView(v);
    if (o) v->setMaximized(v->toplevel()->requested.maximized, o->workArea());
  }

  void Server::requestMinimize(View *v) {
    if (v->toplevel()->requested.minimized && !v->isIconified()) iconifyView(v);
  }
```

`outputForWlr(wlr_output*)` is a tiny private helper mirroring the loop in `outputAt` (`Server.cc:501-503`) — add it, returning the tracked `Output*` for a `wlr_output` or `nullptr`. (The doubled assignment above is a plan typo guard; write it cleanly as:)

```cpp
    Output *target = nullptr;
    if (wlr_output *wo = v->toplevel()->requested.fullscreen_output)
      target = outputForWlr(wo);
    setViewFullscreen(v, want, target);
```

- [ ] **Step 5: Disconnect in the dtor**

`src/View.cc` `~View()` — the `bt::Listener` members auto-disconnect on destruction (RAII, same as the existing `map_`/`commit_`), so no explicit teardown is needed. Confirm by matching how `deco_request_mode_` is handled (it disconnects explicitly only because the decoration outlives differently); the toplevel-owned request listeners follow `map_`/`commit_`/`destroy_` and need nothing.

- [ ] **Step 6: Run to verify it passes**

Container gate, `<tests>` = `xdg_request`. Expected: 3 cases pass. Full gate green; `git status tests/golden/` empty.

- [ ] **Step 7: Commit**

```bash
git add src/View.hh src/View.cc src/Server.hh src/Server.cc \
        tests/harness/TestClient.hh tests/harness/TestClient.cc \
        tests/system/xdg_request_test.cc tests/meson.build
git commit -m "Honor xdg fullscreen/maximize/minimize requests (protocol fix)

We had ZERO listeners for request_maximize/fullscreen/minimize - a live
protocol violation, not just a missing feature: any client sending
set_fullscreen got no configure back. The set_* calls we already make are the
mandated ack. Pre-map requests (mpv --fs) defer to the initial_commit re-run so
set_* never asserts on an uninitialized surface (gotcha #13). request_move/
resize are ack-only v1 - CSD holdouts only, not chased.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01DfRfEGgpiDzryN8MWZDQeb"
```

---

### Task 8: Half-screen snap (keybinding-only)

Classic has no half-snap (only move-time edge thresholds), so this is deliberately new and minimal: stateless resize to the left/right half of the work area, un-maximizing/exiting fullscreen first.

**Files:**
- Modify: `src/Server.hh` / `.cc` (`snapFocused`, `snapFocusedForTest`)
- Create: `tests/system/snap_test.cc`
- Modify: `tests/meson.build` (register `snap`)

**Interfaces:**
- Consumes: `outputForView`, `Output::workArea()`, `currentStyle()->frameMetrics()` (for the border/title insets), `View::setMaximized`/`setFullscreen`/`resizeTo`, `wlr_xdg_toplevel_set_tiled`, `WLR_EDGE_LEFT`/`WLR_EDGE_RIGHT`.
- Produces: `void Server::snapFocused(uint32_t edge)` (edge = `WLR_EDGE_LEFT`/`WLR_EDGE_RIGHT`); `void Server::snapFocusedForTest(uint32_t edge)`. Task 10 wires it to keys.

- [ ] **Step 1: Write the failing test**

Create `tests/system/snap_test.cc`:

```cpp
// Snap: half the WORK area (never the full box - the toolbar's strut stays
// reserved), un-maximizing first. Bar height is derived from the live toolbar
// rect, never baked (work-area discipline).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"
#include "Output.hh"

#include <cstdlib>

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
  }
  View *mapOne(Server &s, test::TestClient &c) {
    REQUIRE(c.ok());
    auto mapped = [&] { const auto &v = s.viewsForTest();
                        return !v.empty() && v.back()->isMapped(); };
    for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); s.dispatch(); c.pump(); }
    REQUIRE(mapped());
    for (int i = 0; i < 30; ++i) { c.flush(); s.dispatch(); c.pump(); }
    return s.viewsForTest().back().get();
  }
  void settle(Server &s, test::TestClient &c) {
    for (int i = 0; i < 40; ++i) { c.flush(); s.dispatch(); c.pump(); }
  }
}

TEST_CASE("SnapLeft/Right halve the work area horizontally") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  server.focusViewForTest(v);
  const wlr_box work = server.activeOutputForTest()->workArea();

  server.snapFocusedForTest(WLR_EDGE_LEFT);
  settle(server, c);
  CHECK(v->x() == work.x);
  const int halfW = work.width / 2;
  // Frame width = content + 2*border; the frame spans the left half exactly.
  CHECK(server.frameWidthForTest(v) == halfW);
  CHECK(server.frameHeightForTest(v) == work.height);

  server.snapFocusedForTest(WLR_EDGE_RIGHT);
  settle(server, c);
  CHECK(v->x() == work.x + work.width - halfW);
}

TEST_CASE("snap un-maximizes first (no double state)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFF00FF00u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  server.focusViewForTest(v);
  const wlr_box work = server.activeOutputForTest()->workArea();
  v->setMaximized(true, work);
  settle(server, c);
  REQUIRE(v->isMaximized());

  server.snapFocusedForTest(WLR_EDGE_LEFT);
  settle(server, c);
  CHECK_FALSE(v->isMaximized());
  CHECK(server.frameWidthForTest(v) == work.width / 2);
}
```

`frameWidthForTest`/`frameHeightForTest` are thin helpers returning `frame::frameWidth(v->contentWidth(), style_->frameMetrics())` etc. — add them (public) if absent, or assert on `contentWidth()` directly with the border math inlined. Register (after `xdg_request`):

```meson
snap_exe = executable('snap-test', files('system/snap_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('snap', snap_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)
```

- [ ] **Step 2: Run to verify it fails**

Container gate, `<tests>` = `snap`.
Expected: BUILD FAILURE — `no member named 'snapFocusedForTest'`.

- [ ] **Step 3: Implement**

`src/Server.hh`, after the fullscreen decls:

```cpp
    void snapFocused(uint32_t edge);   // WLR_EDGE_LEFT/RIGHT -> half the work area
```

Public test wrapper (near the other `*ForTest`):

```cpp
    void snapFocusedForTest(uint32_t edge) { snapFocused(edge); }
```

`src/Server.cc`, after `moveFocusedToOutput`/`snap` neighborhood (place near `setViewFullscreen`):

```cpp
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
```

- [ ] **Step 4: Run to verify it passes**

Container gate, `<tests>` = `snap`. Expected: both cases pass. Full gate green; goldens clean.

- [ ] **Step 5: Commit**

```bash
git add src/Server.hh src/Server.cc tests/system/snap_test.cc tests/meson.build
git commit -m "Half-screen snap to the work area (keybinding-only, new behavior)

Classic never had half-snap, only move-time edge thresholds - so this is
deliberately minimal: stateless resize to half the WORK area (the toolbar strut
stays reserved), un-maximizing/exiting fullscreen first so there's no double
state. set_tiled advertises the edges for clients that drop corners on a snap.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01DfRfEGgpiDzryN8MWZDQeb"
```

---

### Task 9: Move-to-output

**Files:**
- Modify: `src/Server.hh` / `.cc` (`moveFocusedToOutput`, `moveFocusedToOutputForTest`)
- Create: `tests/system/move_to_output_test.cc`
- Modify: `tests/meson.build` (register `move_to_output`)

**Interfaces:**
- Consumes: `wlr_output_layout_adjacent_output(layout, wlr_direction, ref_output, ref_lx, ref_ly)` (POC-proven: RIGHT/LEFT resolve, past-edge → NULL), `outputForView`, `outputForWlr` (Task 7), `Output::fullBox()`/`workArea()`, `View::remaximize`/`setFullscreen`/`setPosition`.
- Produces: `void Server::moveFocusedToOutput(wlr_direction dir)`; `void Server::moveFocusedToOutputForTest(int dir)`.

- [ ] **Step 1: Write the failing test**

Create `tests/system/move_to_output_test.cc`:

```cpp
// Move-to-output: adjacent head in a direction, NULL past the edge (no-op).
// A plain view keeps its offset relative to the target's fullBox (clamped); a
// maximized view re-maximizes onto the target's work area.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"
#include "Output.hh"

#include <cstdlib>

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
  }
  View *mapOne(Server &s, test::TestClient &c) {
    REQUIRE(c.ok());
    auto mapped = [&] { const auto &v = s.viewsForTest();
                        return !v.empty() && v.back()->isMapped(); };
    for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); s.dispatch(); c.pump(); }
    REQUIRE(mapped());
    for (int i = 0; i < 30; ++i) { c.flush(); s.dispatch(); c.pump(); }
    return s.viewsForTest().back().get();
  }
  void settle(Server &s, test::TestClient &c) {
    for (int i = 0; i < 40; ++i) { c.flush(); s.dispatch(); c.pump(); }
  }
}

TEST_CASE("Super+Ctrl+Right moves the frame onto the right head; edge is a no-op") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  server.addHeadlessOutputForTest(1280, 720);       // second head at x=1280 (POC layout)
  for (int i = 0; i < 50 && server.outputCountForTest() != 2; ++i) server.dispatch();
  REQUIRE(server.outputCountForTest() == 2);

  test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  server.focusViewForTest(v);
  v->setPosition(200, 200);                          // on the primary (o0)
  settle(server, c);

  server.moveFocusedToOutputForTest(WLR_DIRECTION_RIGHT);
  settle(server, c);
  const wlr_box o1 = server.outputForTest(1)->fullBox();
  CHECK(v->x() >= o1.x);
  CHECK(v->x() <  o1.x + o1.width);

  // Already on the rightmost head: RIGHT resolves to NULL -> no move.
  const int xr = v->x();
  server.moveFocusedToOutputForTest(WLR_DIRECTION_RIGHT);
  settle(server, c);
  CHECK(v->x() == xr);
}

TEST_CASE("a maximized view re-maximizes onto the target's work area") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  server.addHeadlessOutputForTest(1280, 720);
  for (int i = 0; i < 50 && server.outputCountForTest() != 2; ++i) server.dispatch();
  REQUIRE(server.outputCountForTest() == 2);

  test::TestClient c(server.socketName(), 0xFF0000FFu, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  server.focusViewForTest(v);
  v->setMaximized(true, server.outputForTest(0)->workArea());
  settle(server, c);

  server.moveFocusedToOutputForTest(WLR_DIRECTION_RIGHT);
  settle(server, c);
  const wlr_box w1 = server.outputForTest(1)->workArea();
  CHECK(v->isMaximized());
  CHECK(v->x() == w1.x);
}
```

Register (after `snap`):

```meson
move_to_output_exe = executable('move-to-output-test', files('system/move_to_output_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('move_to_output', move_to_output_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)
```

- [ ] **Step 2: Run to verify it fails**

Container gate, `<tests>` = `move_to_output`.
Expected: BUILD FAILURE — `no member named 'moveFocusedToOutputForTest'`.

- [ ] **Step 3: Implement**

`src/Server.hh`:

```cpp
    void moveFocusedToOutput(wlr_direction dir);   // adjacent head; NULL past edge = no-op
```

Public wrapper:

```cpp
    void moveFocusedToOutputForTest(int dir) {
      moveFocusedToOutput(static_cast<wlr_direction>(dir));
    }
```

`src/Server.cc`:

```cpp
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

    if (v->isFullscreen()) {
      setViewFullscreen(v, false);           // re-apply on the new head's fullBox
      setViewFullscreen(v, true, dst);
      return;
    }
    if (v->isMaximized()) {
      v->remaximize(dst->workArea());
      return;
    }
    // Plain view: preserve the offset within the source head, clamp onto the
    // target so it can't land off-screen on a smaller monitor.
    const wlr_box db = dst->fullBox();
    int nx = db.x + (v->x() - sb.x);
    int ny = db.y + (v->y() - sb.y);
    if (nx > db.x + db.width  - 1) nx = db.x + db.width  - 1;
    if (ny > db.y + db.height - 1) ny = db.y + db.height - 1;
    if (nx < db.x) nx = db.x;
    if (ny < db.y) ny = db.y;
    v->setPosition(nx, ny);
  }
```

- [ ] **Step 4: Run to verify it passes**

Container gate, `<tests>` = `move_to_output`. Expected: both cases pass. Full gate green; goldens clean.

- [ ] **Step 5: Commit**

```bash
git add src/Server.hh src/Server.cc tests/system/move_to_output_test.cc tests/meson.build
git commit -m "Move-to-output: adjacent head, NULL past the edge

wlr_output_layout_adjacent_output resolves the neighbour in a direction and
returns NULL past the layout edge (POC-proven) - a clean no-op there. A plain
view keeps its offset clamped onto the target; maximized re-maximizes onto the
target work area; fullscreen re-applies on the target fullBox. Wired to keys
next.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01DfRfEGgpiDzryN8MWZDQeb"
```

---

### Task 10: Keybindings — the four locked actions

Now that the behavior methods exist, add the `Action::Kind` values, default bindings, and `executeAction` cases in one commit (the switch is exhaustive-by-convention with no `default:` — adding values without cases would warn under `warning_level=3`).

**Files:**
- Modify: `src/Keybindings.hh` (enum) + `src/Keybindings.cc` (default table)
- Modify: `src/Server.cc` (`executeAction` cases, line 1165-1183)
- Modify: `tests/unit/keybinding_test.cc` (the four bindings)
- Create: `tests/system/window_ops_key_test.cc`
- Modify: `tests/meson.build` (register `window_ops_key`)

**Interfaces:**
- Consumes: `setViewFullscreen`/`snapFocused`/`moveFocusedToOutput` (Tasks 5/8/9), `WLR_DIRECTION_*`, `WLR_EDGE_LEFT`/`RIGHT`.
- Produces: `Action::Kind::{ToggleFullscreen, SnapLeft, SnapRight, MoveToOutput}` (PINNED SEAM). `MoveToOutput` carries the direction in `Action::arg`.

- [ ] **Step 1: Write the failing unit test**

Append to `tests/unit/keybinding_test.cc`:

```cpp
TEST_CASE("wave-2 window-mgmt bindings") {
  Keybindings kb;
  const uint32_t CTRL = WLR_MODIFIER_CTRL;
  CHECK(kb.dispatch(SUPER, XKB_KEY_f).kind == Action::ToggleFullscreen);
  CHECK(kb.dispatch(SUPER | SHIFT, XKB_KEY_Left).kind  == Action::SnapLeft);
  CHECK(kb.dispatch(SUPER | SHIFT, XKB_KEY_Right).kind == Action::SnapRight);

  Action ml = kb.dispatch(SUPER | CTRL, XKB_KEY_Left);
  CHECK(ml.kind == Action::MoveToOutput);
  CHECK(ml.arg == WLR_DIRECTION_LEFT);
  Action md = kb.dispatch(SUPER | CTRL, XKB_KEY_Down);
  CHECK(md.kind == Action::MoveToOutput);
  CHECK(md.arg == WLR_DIRECTION_DOWN);

  // Plain Super+Left still switches workspaces (must not be shadowed).
  CHECK(kb.dispatch(SUPER, XKB_KEY_Left).kind == Action::WorkspacePrev);
}
```

- [ ] **Step 2: Run to verify it fails**

Container gate, `<tests>` = `unit`.
Expected: BUILD FAILURE — `'ToggleFullscreen' is not a member of 'bbai::Action'`.

- [ ] **Step 3: Implement**

`src/Keybindings.hh`, extend the enum (line 17-21):

```cpp
    enum Kind {
      None, WorkspaceNext, WorkspacePrev, WorkspaceTo,
      OpenMenu, CloseWindow, CycleNext, CyclePrev,
      IconMenu, Screenshot, Quit,
      ToggleFullscreen, SnapLeft, SnapRight, MoveToOutput
    };
```

`src/Keybindings.cc`, add to `bindings_` (after the Quit binding, line 27; `arg` reuses the WorkspaceTo pattern for the direction):

```cpp
      { SUPER,         XKB_KEY_f,     { Action::ToggleFullscreen } },
      { SUPER | SHIFT, XKB_KEY_Left,  { Action::SnapLeft } },
      { SUPER | SHIFT, XKB_KEY_Right, { Action::SnapRight } },
      { SUPER | CTRL,  XKB_KEY_Left,  { Action::MoveToOutput, WLR_DIRECTION_LEFT } },
      { SUPER | CTRL,  XKB_KEY_Right, { Action::MoveToOutput, WLR_DIRECTION_RIGHT } },
      { SUPER | CTRL,  XKB_KEY_Up,    { Action::MoveToOutput, WLR_DIRECTION_UP } },
      { SUPER | CTRL,  XKB_KEY_Down,  { Action::MoveToOutput, WLR_DIRECTION_DOWN } },
```

`src/Server.cc`, add cases in `executeAction` before `case Action::None:` (line 1182):

```cpp
    case Action::ToggleFullscreen:
      if (focused_view) setViewFullscreen(focused_view, !focused_view->isFullscreen());
      break;
    case Action::SnapLeft:     snapFocused(WLR_EDGE_LEFT);  break;
    case Action::SnapRight:    snapFocused(WLR_EDGE_RIGHT); break;
    case Action::MoveToOutput: moveFocusedToOutput(static_cast<wlr_direction>(a.arg)); break;
```

- [ ] **Step 4: Write the failing system test (keys drive the behavior)**

Create `tests/system/window_ops_key_test.cc`:

```cpp
// The locked keys reach the behaviors through the real onKey/executeAction path
// (injectKeyForTest mirrors it). One case is enough per action - the geometry
// is already pinned by fullscreen/snap tests; this proves the wiring.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
  }
  View *mapOne(Server &s, test::TestClient &c) {
    REQUIRE(c.ok());
    auto mapped = [&] { const auto &v = s.viewsForTest();
                        return !v.empty() && v.back()->isMapped(); };
    for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); s.dispatch(); c.pump(); }
    REQUIRE(mapped());
    for (int i = 0; i < 30; ++i) { c.flush(); s.dispatch(); c.pump(); }
    return s.viewsForTest().back().get();
  }
}

TEST_CASE("Super+F toggles fullscreen on the focused view") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapOne(server, c);
  server.focusViewForTest(v);

  server.injectKeyForTest(XKB_KEY_f, WLR_MODIFIER_LOGO, true);
  server.injectKeyForTest(XKB_KEY_f, WLR_MODIFIER_LOGO, false);
  for (int i = 0; i < 40; ++i) { c.flush(); server.dispatch(); c.pump(); }
  CHECK(v->isFullscreen());

  server.injectKeyForTest(XKB_KEY_f, WLR_MODIFIER_LOGO, true);
  server.injectKeyForTest(XKB_KEY_f, WLR_MODIFIER_LOGO, false);
  for (int i = 0; i < 40; ++i) { c.flush(); server.dispatch(); c.pump(); }
  CHECK_FALSE(v->isFullscreen());
}
```

Register (after `move_to_output`):

```meson
window_ops_key_exe = executable('window-ops-key-test', files('system/window_ops_key_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('window_ops_key', window_ops_key_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)
```

- [ ] **Step 5: Run to verify it passes**

Container gate, `<tests>` = `unit window_ops_key`. Expected: both green. Full gate green; goldens clean.

- [ ] **Step 6: Commit**

```bash
git add src/Keybindings.hh src/Keybindings.cc src/Server.cc \
        tests/unit/keybinding_test.cc tests/system/window_ops_key_test.cc tests/meson.build
git commit -m "Bind fullscreen/snap/move-to-output to the locked keys

Super+F, Super+Shift+Left/Right, Super+Ctrl+arrows - the plain Super+arrows
workspace switch stays untouched (equality-mask matcher: +Ctrl/+Shift is a
different binding). MoveToOutput carries the wlr_direction in Action.arg,
reusing the WorkspaceTo arg pattern.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01DfRfEGgpiDzryN8MWZDQeb"
```

---

### Task 11: Sloppy focus (default-on) + the modal-interaction guards

The user-locked behavior and the most trap-dense change in the slice. `onPointerMotion` is the repo's most gate-heavy function; sloppy refocus rides its EXISTING early-returns (lock at :890, modal menu at :891, screenshot at :908, implicit-grab at :920) and adds one `!cycling_` guard so an alt-tab preview isn't fought. The three interaction tests the prompt demands are the whole point of this task.

**Files:**
- Modify: `src/Config.hh` (focus default → SloppyFocus) + `src/Config.cc` (parse fallback default)
- Modify: `src/Server.cc` (`onPointerMotion` refocus at the client-hit tail, ~line 928-937)
- Modify: `tests/unit/config_test.cc` (the two default assertions that pin ClickToFocus)
- Create: `tests/system/sloppy_focus_test.cc`
- Modify: `tests/meson.build` (register `sloppy_focus`)

**Interfaces:**
- Consumes: `config().focusModel`, `focusView` (already lock-guarded, returns early while locked), `cycling_`, the existing `viewFromNode`/`partAt` hit-test in `onPointerMotion`.
- Produces: focus-follows-mouse behind the four gates. Task 12 adds AutoRaise/ClickRaise on top.

- [ ] **Step 1: Flip the Config default + fix the pinned tests**

`src/Config.hh` line 67:

```cpp
    // Sloppy focus is the product default (user-locked: focus-follows-mouse
    // default-on). An rc session.focusModel key always wins.
    FocusModel focusModel = FocusModel::SloppyFocus;
```

`src/Config.cc`, the parse fallback (line 92) — an ABSENT key now yields SloppyFocus, not ClickToFocus:

```cpp
                        "SloppyFocus"));
```

`tests/unit/config_test.cc` lines 20-21 (the empty-config defaults):

```cpp
  CHECK(c.focusModel == FocusModel::SloppyFocus);   // user-locked default-on
  CHECK(c.autoRaise == false);                      // no AutoRaise by default
```

(Leave the explicit-`ClickToFocus` cases at lines 38/50/73-78/157 alone — they set the key, so they still hold. Only the default-constructed/empty-parse assertions flip.)

- [ ] **Step 2: Write the failing system test (behavior + the three guards)**

Create `tests/system/sloppy_focus_test.cc`:

```cpp
// Focus-follows-mouse, default-on. The refocus rides onPointerMotion's existing
// early-returns, so the three trap cases are: motion while LOCKED changes
// nothing, motion during an OPEN MENU changes nothing, motion during an ALT-TAB
// preview doesn't scramble the frozen ring. Two clients, non-overlapping.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
  }
  View *mapAt(Server &s, test::TestClient &c, int x, int y) {
    REQUIRE(c.ok());
    auto mapped = [&] { const auto &v = s.viewsForTest();
                        return !v.empty() && v.back()->isMapped(); };
    for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); s.dispatch(); c.pump(); }
    REQUIRE(mapped());
    View *v = s.viewsForTest().back().get();
    v->setPosition(x, y);
    for (int i = 0; i < 30; ++i) { c.flush(); s.dispatch(); c.pump(); }
    return v;
  }
  // Client area of an SSD frame at (fx,fy): x in [fx+1, ...), y in [fy+23, ...).
  int clientX(View *v) { return v->x() + 30; }
  int clientY(View *v) { return v->y() + 40; }
}

TEST_CASE("hovering a window's client area focuses it (sloppy default-on)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);   // no rc -> SloppyFocus default
  boot(server);
  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *a = mapAt(server, ca, 100, 100);
  test::TestClient cb(server.socketName(), 0xFF0000FFu, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *b = mapAt(server, cb, 100, 400);   // B mapped last -> focused (focusNewWindows)
  REQUIRE(server.focusedViewForTest() == b);

  server.injectPointerMotionForTest(clientX(a), clientY(a));
  for (int i = 0; i < 10; ++i) { ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump(); }
  CHECK(server.focusedViewForTest() == a);    // followed the mouse
}

TEST_CASE("motion while LOCKED does not change focus") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *a = mapAt(server, ca, 100, 100);
  server.lockForTest();
  REQUIRE(server.focusedViewForTest() == nullptr);   // lock parks focus
  server.injectPointerMotionForTest(clientX(a), clientY(a));
  for (int i = 0; i < 10; ++i) { ca.flush(); server.dispatch(); ca.pump(); }
  CHECK(server.focusedViewForTest() == nullptr);     // still parked
}

TEST_CASE("motion during an open modal menu does not change focus") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *a = mapAt(server, ca, 100, 100);
  test::TestClient cb(server.socketName(), 0xFF0000FFu, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *b = mapAt(server, cb, 100, 400);
  server.focusViewForTest(b);

  // Right-click the bare desktop to open the root menu, then wander over A.
  server.injectPointerMotionForTest(700, 300);
  server.injectPointerButtonForTest(BTN_RIGHT, true);
  REQUIRE(server.menuOpenForTest());
  server.injectPointerMotionForTest(clientX(a), clientY(a));
  for (int i = 0; i < 10; ++i) { ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump(); }
  CHECK(server.focusedViewForTest() == b);   // menu is modal; focus unmoved
  server.injectKeyForTest(XKB_KEY_Escape, 0, true);
}

TEST_CASE("motion during an alt-tab preview does not fight the cycle") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *a = mapAt(server, ca, 100, 100);
  test::TestClient cb(server.socketName(), 0xFF0000FFu, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *b = mapAt(server, cb, 100, 400);   // b focused, MRU front

  server.injectKeyForTest(XKB_KEY_Tab, WLR_MODIFIER_ALT, true);   // preview -> a
  REQUIRE(server.cyclingForTest());
  const View *preview = server.focusedViewForTest();
  // A stray hover over B mid-cycle must NOT refocus B and scramble the ring.
  server.injectPointerMotionForTest(clientX(b), clientY(b));
  for (int i = 0; i < 5; ++i) { ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump(); }
  CHECK(server.focusedViewForTest() == preview);   // cycle preview intact
  CHECK(server.cyclingForTest());
  server.injectKeyForTest(XKB_KEY_Alt_L, 0, false); // commit
}
```

Register (after `window_ops_key`), `text_env` for the menu case's font-derived rows:

```meson
sloppy_focus_exe = executable('sloppy-focus-test', files('system/sloppy_focus_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('sloppy_focus', sloppy_focus_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)
```

`cyclingForTest()` / `menuOpenForTest()` — reuse if present (`alttab_test.cc` / `menu_action_test.cc` drive these), else add trivial accessors returning `cycling_` / `active_menu_ != nullptr`.

- [ ] **Step 3: Run to verify it fails**

Container gate, `<tests>` = `sloppy_focus unit`.
Expected: the first case fails (hover doesn't refocus — no sloppy code yet); `unit` `config_test` now passes with the flipped defaults.

- [ ] **Step 4: Implement the refocus**

`src/Server.cc`, in `onPointerMotion`, at the client hit-test tail (lines 928-937), refocus BEFORE forwarding the pointer. Every disqualifying state already returned above (locked :890, menu :891, screenshot :908, implicit-grab :920); the one new guard is `!cycling_`:

```cpp
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
      if (config_.focusModel == FocusModel::SloppyFocus && !cycling_ && v != focused_view)
        focusView(v);
      wlr_surface *surf = v->toplevel()->base->surface;
      wlr_seat_pointer_notify_enter(seat, surf, sx, sy);
      wlr_seat_pointer_notify_motion(seat, time, sx, sy);
    } else {
      wlr_seat_pointer_notify_clear_focus(seat);
    }
```

- [ ] **Step 5: Run to verify it passes AND prove zero golden churn**

Container gate, `<tests>` = `sloppy_focus`. Expected: 4 cases pass. Then the FULL gate. This is the moment sloppy-default-on could churn a wave-1 golden: `git status tests/golden/` MUST be empty and every wave-1 focus/golden test MUST stay green. If a golden or a focus assertion flips, the cause is a test whose cursor genuinely rests over a *different* window's client than its intended focus — such a test's premise was click-to-focus. Fix it by prepending an explicit `session.focusModel: ClickToFocus\n` rc to THAT test (a test-mechanism change, not a behavior regression, and not a re-bless). Do NOT `BLESS=1` anything. (Expected: `focus_swap_test` and friends stay green untouched — their cursor ends over the window they focus, or over a titlebar which is not Part::Client, so sloppy is a no-op there.)

- [ ] **Step 6: Commit**

```bash
git add src/Config.hh src/Config.cc src/Server.cc \
        tests/unit/config_test.cc tests/system/sloppy_focus_test.cc tests/meson.build
git commit -m "Focus-follows-mouse, default-on (user-locked)

SloppyFocus becomes the key-less default; an rc always wins. The refocus rides
onPointerMotion's existing early-returns - lock, open menu, screenshot and the
implicit grab all bail before it - and adds one !cycling_ guard so a stray hover
mid-alt-tab can't scramble the frozen ring. Gated to Part::Client so hovering
our own chrome doesn't thrash focus, which is also why the wave-1 focus goldens
don't churn (their cursors rest on titlebars or the focused window). Zero
re-bless.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01DfRfEGgpiDzryN8MWZDQeb"
```

---

### Task 12: AutoRaise + ClickRaise

The two sloppy sub-flags (parsed already; inert under ClickToFocus). AutoRaise raises the focused-by-hover window after `autoRaiseDelay` on an injectable Timer; ClickRaise raises a window on a click in it.

**Files:**
- Modify: `src/Server.hh` (an `autoraise_` Timer + handler + pending view)
- Modify: `src/Server.cc` (arm on sloppy refocus; fire → raise; ClickRaise in `onPointerButton`)
- Create: `tests/system/auto_raise_test.cc`
- Modify: `tests/meson.build` (register `auto_raise`)

**Interfaces:**
- Consumes: `config().autoRaise`/`clickRaise`/`autoRaiseDelay`, `raiseView`, `Timer`/`TimeoutHandler` (`Timer.hh`), `advanceClockForTest` (`Server.cc:1571`).
- Produces: AutoRaise/ClickRaise behavior. No new seam.

- [ ] **Step 1: Write the failing test**

Create `tests/system/auto_raise_test.cc`:

```cpp
// AutoRaise: after focus-follows-mouse settles on a window, it raises after
// autoRaiseDelay (400ms default) - driven by the VirtualClock. ClickRaise:
// a click in a window raises it immediately. Both are sloppy sub-flags, so the
// rc sets SloppyFocus explicitly.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>
#include <linux/input-event-codes.h>

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
  }
  std::string writeRc(const std::string &body) {
    char t[] = "/tmp/bbai-raise-XXXXXX";
    REQUIRE(mkdtemp(t) != nullptr);
    std::string p = std::string(t) + "/rc";
    std::ofstream(p) << body;
    return p;
  }
  View *mapAt(Server &s, test::TestClient &c, int x, int y) {
    REQUIRE(c.ok());
    auto mapped = [&] { const auto &v = s.viewsForTest();
                        return !v.empty() && v.back()->isMapped(); };
    for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); s.dispatch(); c.pump(); }
    REQUIRE(mapped());
    View *v = s.viewsForTest().back().get();
    v->setPosition(x, y);
    for (int i = 0; i < 30; ++i) { c.flush(); s.dispatch(); c.pump(); }
    return v;
  }
}

TEST_CASE("AutoRaise raises the hovered window after the delay") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  const std::string rc = writeRc("session.focusModel: SloppyFocus AutoRaise\n");
  Server server(/*headless=*/true, rc);
  boot(server);
  // A behind B (B mapped last, raised). Hover A: focus follows immediately, but
  // the RAISE waits for the timer.
  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *a = mapAt(server, ca, 150, 150);
  test::TestClient cb(server.socketName(), 0xFF0000FFu, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *b = mapAt(server, cb, 200, 200);

  server.injectPointerMotionForTest(a->x() + 30, a->y() + 40);
  for (int i = 0; i < 10; ++i) { ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump(); }
  REQUIRE(server.focusedViewForTest() == a);
  CHECK_FALSE(server.isTopmostForTest(a));    // focused but not yet raised
  server.advanceClockForTest(1);              // 1s > 400ms one-shot -> fires
  CHECK(server.isTopmostForTest(a));          // now raised
}

TEST_CASE("ClickRaise raises on a click in the window") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  const std::string rc = writeRc("session.focusModel: SloppyFocus ClickRaise\n");
  Server server(/*headless=*/true, rc);
  boot(server);
  test::TestClient ca(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *a = mapAt(server, ca, 150, 150);
  test::TestClient cb(server.socketName(), 0xFF0000FFu, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *b = mapAt(server, cb, 200, 200);
  REQUIRE(server.isTopmostForTest(b));

  // Click in A's client area: focus + raise, immediately (no timer).
  server.injectPointerMotionForTest(a->x() + 30, a->y() + 40);
  server.injectPointerButtonForTest(BTN_LEFT, true);
  server.injectPointerButtonForTest(BTN_LEFT, false);
  for (int i = 0; i < 20; ++i) { ca.flush(); cb.flush(); server.dispatch(); ca.pump(); cb.pump(); }
  CHECK(server.isTopmostForTest(a));
}
```

Register (after `sloppy_focus`):

```meson
auto_raise_exe = executable('auto-raise-test', files('system/auto_raise_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('auto_raise', auto_raise_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)
```

`isTopmostForTest(View*)` returns whether the view is `stacking_.front()` (the topmost real entity) — add it (public) using `topmostViewOnWorkspace(v->workspace()) == v` or a direct `stacking_.front()` cast.

- [ ] **Step 2: Run to verify it fails**

Container gate, `<tests>` = `auto_raise`.
Expected: BUILD FAILURE (`isTopmostForTest`) or the AutoRaise CHECK fails (no timer wired).

- [ ] **Step 3: Implement**

`src/Server.hh`, add near the Toolbar's timer pattern (private, after `keybindings_` ~line 316):

```cpp
    // AutoRaise: a one-shot timer armed when sloppy focus settles on a window;
    // on fire, raise it if it's still the focused one. Injectable Timer -> the
    // VirtualClock drives it deterministically in tests.
    struct AutoRaiseTick : TimeoutHandler {
      explicit AutoRaiseTick(Server *s) : srv(s) {}
      Server *srv;
      void timeout() override { srv->onAutoRaiseTimeout(); }
    };
    AutoRaiseTick autoraise_handler_{ this };
    std::unique_ptr<Timer> autoraise_timer_;
    View *autoraise_pending_ = nullptr;
    void armAutoRaise(View *v);       // (re)start the one-shot for v, or cancel
    void onAutoRaiseTimeout();
```

Construct the timer once the registry exists (in the ctor, after `timer_registry_` is built ~line 231):

```cpp
    autoraise_timer_ = std::make_unique<Timer>(*timer_registry_, autoraise_handler_);
```

Reset it in the dtor before the registry (near `toolbar_.reset()` ~line 404):

```cpp
    autoraise_timer_.reset();   // deregisters before the TimerRegistry dies
```

`src/Server.cc`, add the arm/fire pair:

```cpp
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
```

Arm it from the sloppy refocus in `onPointerMotion` (extend the Task 11 block):

```cpp
      if (config_.focusModel == FocusModel::SloppyFocus && !cycling_ && v != focused_view) {
        focusView(v);
        armAutoRaise(v);   // AutoRaise off -> a no-op that just cancels any pending
      }
```

ClickRaise in `onPointerButton`, at the client-hit PRESSED branch (right after `focusView(v);` at line 1004):

```cpp
        if (config_.clickRaise) raiseView(v);   // sloppy sub-flag; inert under CTF
```

- [ ] **Step 4: Run to verify it passes**

Container gate, `<tests>` = `auto_raise`. Expected: both cases pass. Full gate green; `git status tests/golden/` empty.

- [ ] **Step 5: Commit**

```bash
git add src/Server.hh src/Server.cc tests/system/auto_raise_test.cc tests/meson.build
git commit -m "AutoRaise + ClickRaise (sloppy sub-flags)

AutoRaise arms a one-shot Timer on each focus-follows-mouse settle and raises
the window if it's still focused when the delay elapses (VirtualClock-driven, so
tests are deterministic). ClickRaise raises on a click in the client. Both inert
under ClickToFocus (the parser zeroes the sub-flags), so no behavior change for
a click-to-focus rc.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01DfRfEGgpiDzryN8MWZDQeb"
```

---

## Placement (Cascade / Center / RowSmart) — folded into the map path

The wave-2 decision pairs sloppy focus with "Cascade/Center/RowSmart placement replacing the fixed (160,120) map position." That belongs in `Server::onViewMapped` (`Server.cc:447`), not `View`, and reads `config().windowPlacement` (parsed wave-1). It is intentionally the LAST behavior added, because it moves where every window opens and so has the widest golden blast radius of the slice.

### Task 13: Window placement at map

**Files:**
- Create: `src/Placement.geom.hh` (pure placement math)
- Create: `tests/unit/placement_geom_test.cc`
- Modify: `src/Server.cc` (`onViewMapped` computes + applies the map position)
- Modify: `src/Server.hh` (a small `placement_next_` cascade cursor if Cascade needs one)
- Create: `tests/system/placement_test.cc`
- Modify: `tests/meson.build` (register both)

**Interfaces:**
- Consumes: `config().windowPlacement`, `Output::workArea()`, the frame width/height helpers (`Frame.hh`), the set of already-mapped views on the target workspace.
- Produces: `bbai::place::Point place(WindowPlacement policy, wlr_box work, int fw, int fh, const std::vector<wlr_box> &taken, int &cascade_cursor)` — pure, unit-tested; `onViewMapped` calls it and `setPosition`s the result.

- [ ] **Step 1: Write the failing unit test**

Create `tests/unit/placement_geom_test.cc`:

```cpp
// Pure placement math (classic Placement.cc semantics, minimal port): Center
// centres in the work area; Cascade steps a fixed diagonal from the work
// origin, wrapping; RowSmart returns the first free left-to-right slot, falling
// back to the origin when the area is full. No wlroots.
#include <doctest/doctest.h>
#include "Placement.geom.hh"

using namespace bbai;
using namespace bbai::place;

TEST_CASE("Center centres the frame in the work area") {
  wlr_box work{0, 0, 1280, 697};      // 720 minus a 23px bottom bar
  int cur = 0;
  Point p = place(WindowPlacement::Center, work, 204, 179, {}, cur);
  CHECK(p.x == (1280 - 204) / 2);
  CHECK(p.y == (697 - 179) / 2);
}

TEST_CASE("Cascade steps a diagonal and wraps at the edge") {
  wlr_box work{0, 0, 1280, 697};
  int cur = 0;
  Point a = place(WindowPlacement::Cascade, work, 204, 179, {}, cur);
  Point b = place(WindowPlacement::Cascade, work, 204, 179, {}, cur);
  CHECK(a.x == 0); CHECK(a.y == 0);
  CHECK(b.x > a.x); CHECK(b.y > a.y);   // stepped down-right
}

TEST_CASE("RowSmart avoids an occupied origin, falls back when full") {
  wlr_box work{0, 0, 1280, 697};
  int cur = 0;
  std::vector<wlr_box> taken{{0, 0, 204, 179}};
  Point p = place(WindowPlacement::RowSmart, work, 204, 179, taken, cur);
  CHECK_FALSE(p.x == 0 && p.y == 0);    // not on top of the taken slot
  CHECK(p.x >= 0); CHECK(p.x + 204 <= 1280);
}
```

Register in `unit_sources` (after the last entry).

- [ ] **Step 2: Run to verify it fails**

Container gate, `<tests>` = `unit`.
Expected: BUILD FAILURE — `Placement.geom.hh: No such file`.

- [ ] **Step 3: Implement the pure header + the map hook**

Create `src/Placement.geom.hh` with `struct Point{int x,y;}` and the `place(...)` function implementing Center (centre in work), Cascade (step `kCascadeStep` = titleHeight-ish diagonal from the work origin, wrap when `x+fw>work.right` or `y+fh>work.bottom` back to origin), and RowSmart (scan left-to-right, top-to-bottom in `fw`/`fh` strides for the first slot not intersecting any `taken` box; fall back to `{work.x, work.y}`). Keep it pure and header-only (the slit/toolbar geom headers are the pattern).

In `Server.cc` `onViewMapped` (line 447), before/replacing the fixed-position default, compute the map position for a freshly-mapped, non-cycling view:

```cpp
    // Place the window per policy instead of the fixed (160,120) ctor default.
    if (Output *o = outputForView(view)) {
      std::vector<wlr_box> taken;
      for (auto &up : views) {
        View *o2 = up.get();
        if (o2 == view || !o2->isMapped() || o2->workspace() != view->workspace()) continue;
        taken.push_back({o2->x(), o2->y(),
                         frame::frameWidth(o2->contentWidth(), currentStyle()->frameMetrics()),
                         frame::frameHeight(o2->contentHeight(), currentStyle()->frameMetrics())});
      }
      const frame::FrameMetrics &fm = currentStyle()->frameMetrics();
      place::Point p = place::place(config_.windowPlacement, o->workArea(),
                                    frame::frameWidth(view->contentWidth(), fm),
                                    frame::frameHeight(view->contentHeight(), fm),
                                    taken, placement_cascade_);
      view->setPosition(p.x, p.y);
    }
```

Add `int placement_cascade_ = 0;` to `Server.hh` (the cascade cursor). Guard the whole block so a maximized/fullscreen-on-map client (rare) isn't repositioned under its own state — only place plain views.

- [ ] **Step 4: Write + register the system test**

Create `tests/system/placement_test.cc`: boot with each `session.windowPlacement` rc, map two clients, assert the second lands per policy (Center → centred; Cascade → offset from the first; RowSmart → not overlapping the first). Derive all expected coordinates from `workArea()` + the frame helpers, never baked. `text_env` (SSD frames carry font-derived titlebars).

- [ ] **Step 5: Run + prove golden discipline**

Container gate, `<tests>` = `unit placement`. Then the FULL gate. Placement moves where windows open, so wave-1 tests that asserted the fixed `(160,120)` (e.g. `focus_swap_test` checks `va->x()==160`) will now see the placed position. Those are THIS slice's tests to update: either the test explicitly `setPosition`s after map (many already do), or it asserts the placed coordinate. `git status tests/golden/` MUST stay empty — the goldens that pin a window at a specific spot either `setPosition` explicitly before capture (unaffected) or are this slice's to keep green by explicit positioning, NEVER by re-bless. If a golden's window would move, add an explicit `setPosition` to restore its captured geometry (mechanism change, not a re-bless).

- [ ] **Step 6: Commit**

```bash
git add src/Placement.geom.hh src/Server.hh src/Server.cc \
        tests/unit/placement_geom_test.cc tests/system/placement_test.cc tests/meson.build
git commit -m "Place mapped windows by policy - kill the fixed (160,120) wart

onViewMapped now runs Cascade/Center/RowSmart off config().windowPlacement
(parsed wave-1) instead of parking every window at the ctor default. Pure
geometry in Placement.geom.hh; the map hook feeds it the occupied frames on the
target workspace. Golden discipline held by explicit setPosition where a pinned
capture would otherwise move - zero re-bless.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01DfRfEGgpiDzryN8MWZDQeb"
```

---

## TTY checkpoint additions (accumulating hand-check list — headless can't reach these)

Headless has no real input device (gotcha #15) and no screen to point at, so these go on the program plan's hand-check list, owed by this slice:
- Real wheel scroll into a client (Firefox/terminal), over the toolbar, and over the desktop — the last two switch workspaces.
- `mpv --fs` and Firefox `F11`: fullscreen enter/exit, chrome gone, toolbar covered; `Super+F` on a normal window.
- Move-to-output on a real dual head (`Super+Ctrl+Right`), including a maximized and a fullscreen window.
- Focus-follows-mouse feel on real hardware + AutoRaise timing at the default 400ms; snap left/right tiling by key.

## Self-review

**Spec coverage.** Fullscreen toggle + protocol fix (Tasks 1,5,6,7); axis defect + wheel bools + wheel-workspace (Tasks 2,3,4); `Toolbar::containsGlobal` (Task 4); `injectPointerAxisForTest` + mid-drag test (Task 3); `layer_fullscreen` (Task 6); snap (Task 8); move-to-output (Task 9); keys locked (Task 10); sloppy focus default-on + the three modal-interaction tests (Task 11); AutoRaise/ClickRaise (Task 12); Cascade/Center/RowSmart placement (Task 13); WM_CAPABILITIES named POC (Task 1); wheel-default verification with the seam-text rebuttal (Task 2). Config.* ownership resolved (Global Constraints + Task 2). Every pinned PROVIDES seam is a named deliverable with its verbatim signature.

**Placeholder scan.** No TBD/TODO; every code step carries real code; the two `[Task N inserts here]` markers are explicit hand-offs between this slice's own sequential tasks, each with the exact replacement shown at the later task — not open-ended placeholders.

**Type consistency.** `setViewFullscreen(View*, bool, Output* = nullptr)`, `View::setFullscreen(bool, wlr_box)`/`isFullscreen()`, `snapFocused(uint32_t edge)`, `moveFocusedToOutput(wlr_direction)`, `onPointerAxis(uint32_t, wl_pointer_axis, double, int32_t, wl_pointer_axis_source, wl_pointer_axis_relative_direction)`, `injectPointerAxisForTest(wl_pointer_axis, double, int32_t)`, `Toolbar::containsGlobal(int,int)`, `Action::{ToggleFullscreen,SnapLeft,SnapRight,MoveToOutput}` with the direction in `Action::arg` — used identically wherever they recur.

**Known soft spots flagged for the implementer, not hidden:** several `*ForTest` introspection hooks (`lockForTest`, `idleActivityCountForTest`, `currentWorkspaceForTest`, `focusViewForTest`, `menuOpenForTest`, `cyclingForTest`, `isTopmostForTest`, `viewLayerIsFullscreenForTest`, `frameWidthForTest`/`frameHeightForTest`) are assumed-or-add: each task says to reuse the existing accessor if a sibling wave-1 test already exposes it, else add the trivial one-liner in that task. The `outputForWlr` helper and the `fullscreen_before_map` TestClient flag are small additive harness pieces called out at their first use. None of these change product behavior; they exist so every behavior is headlessly asserted.
