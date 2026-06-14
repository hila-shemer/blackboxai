# BlackboxAI Milestone 4 — Workspaces + Toolbar/clock + Root menu + Keybindings — Plan

**Goal:** A bottom toolbar shows the current workspace name, the focused window
title, and a **ticking clock** (driven by an injectable virtual clock, never
wall-clock); **right-clicking the desktop** opens a modal Blackbox **root menu**
(compositor chrome) whose items exec apps / switch workspaces / exit; a
**keybinding layer** (Mod4-based) switches workspaces, opens the menu, cycles and
closes windows; **switching workspaces** shows/hides the right windows with
correct restacking and focus hygiene. All built-in defaults (drop-in
`.blackboxrc`/style/menu-file parsing is **M5**). Proven headless (golden-PNG +
L0 unit + keybinding/menu interaction tests); combined `toolkit/`+`src/` line
coverage ≥80%; M1–M3 goldens still pass.

**Builds on M1–M3.** Reuses the renderer seam (`bt::Texture` →
`bt::Image::renderBuffer` → `DataBuffer` → `wlr_scene_buffer`), `bt::TextRenderer`,
the 5 fixed scene layers, `wlr_seat`/`wlr_cursor`, the `onPointer*` grab state
machine + `injectPointer*ForTest`, and the `nowMsec()` monotonic time seam.

Grounded by a 7-agent design-research pass (verified against the installed
wlroots 0.19.3 / xkbcommon headers and the vendored `reference/blackboxwm`).

## User decisions locked this session
- **Mid-way checkpoint**: land **Phase A** (pure data models + toolbar/clock) to a
  green, reviewable state and **pause for review**; then **Phase B** (keyboard +
  keybindings + root menu + workspace switching).
- **Ultracode**: parallel design research (done) + an adversarial review before
  push (after Phase B).

## Key architecture decisions (resolved from the research critique)
- **Deviceless key injection.** `wlr_headless_add_input_device` does **not** exist
  in 0.19 (verified). Production funnels real `wlr_keyboard.events.key` →
  `Server::onKey(kb, time, evdev_keycode, state)` which does the evdev→XKB **`+8`**
  offset, `xkb_state_key_get_syms`, `wlr_keyboard_get_modifiers`, then
  `Keybindings::dispatch(mods, sym)`. Tests call `injectKeyForTest(sym, mods,
  pressed)` → the **same matcher** (mirrors `injectPointerButtonForTest`). A
  separate **pure xkb unit test** pins `evdev+8` + `xkb_state_key_get_one_sym` so
  the offset seam the injector bypasses is still covered.
- **Modal menu = a compositor-side mode, not a `wlr_seat` grab.** `Server` holds
  `std::unique_ptr<Menu> active_menu_`; each `onPointer*`/`onKey` handler checks
  `active_menu_ != nullptr` **first** (same pattern as `CursorMode`), so the menu
  and the move/resize grab are mutually exclusive by construction.
- **Workspace switch = `wlr_scene_node_set_enabled` on each View's frame tree**
  (O(1), preserves stacking), **not** reparent. Disabling a node does **not** clear
  `wlr_seat` focus, so the switch must explicitly restore/clear keyboard focus and
  re-run a pointer hit-test (the same close-hygiene menus use).
- **Clock determinism: `gmtime_r`, not `localtime_r`.** Faithful blackbox format
  `"%I:%M %p"`, but rendered via `gmtime_r` so goldens don't depend on CI's `$TZ`;
  the injected epoch is chosen so the UTC rendering is the asserted string
  (`14:05:00 UTC` → `"02:05 PM"`). The `nowMsec()` event counter is **not** reused
  as a wall clock — a separate `bt::Clock` seam owns wall time.
- **Geometry constants are fixed (M3 style), derived from blackbox's `textHeight`
  convention (15), NOT from fcft's runtime `font->height()`** (which is 18 for
  LiberationMono@16). The real invariant is "rendered text height ≤ the 19px label
  rect" (18 ≤ 19), already true. Section widths are computed at runtime via
  `title_font.textWidth(...)`; the golden PNG is the geometry source of truth — do
  **not** hardcode pixel widths like `103` as code constants.
- **Right-click opens the root menu only over the desktop background**, never over
  toolbar/menu chrome. `viewFromNode(n)==nullptr` is necessary but not sufficient
  (toolbar nodes also yield null), so add a guard (hit node not under
  `layer_top`/`layer_overlay`, or `cursor->y < toolbar_top`).
- **Exec via an injectable `CommandRunner` taking `argv`** (no `/bin/sh`,
  parser-free — items are built-in); `PosixCommandRunner` double-forks/`setsid` and
  sets `WAYLAND_DISPLAY`; `FakeCommandRunner` records argv in tests (nothing spawns).
- **Swallowed-key bookkeeping**: a bound PRESS that is swallowed must also swallow
  its matching RELEASE (`std::set<uint32_t> swallowed_keycodes_`), else the client
  gets an orphan release.

## Built-in defaults (hardcoded in M4; configurable in M5)
- **Workspaces**: 4, named `"Workspace 1".."Workspace 4"`, start on index 0.
- **Toolbar**: bottom-center, 66% output width → `{x:218, y:697, w:844, h:23}` at
  1280×720; sections `workspace-label | window-label | clock | [arrows]`; clock
  `"%I:%M %p"`, 60s tick.
- **Keybindings** (Mod4 = Logo): `Left`/`Right` prev/next workspace, `1`..`4` switch
  to N, `space` open root menu, `q` close focused window, `Tab`/`Shift+Tab` cycle
  focus.
- **Root menu** (built-in tree): `foot` (exec), `xterm` (exec), `Workspaces`
  submenu (the 4 workspaces with a ✓ on current + New/Remove), `Restart` (stub),
  `Exit` (`wl_display_terminate`).

---

## Verified APIs (against installed headers)
- **Keyboard** (`wlr_keyboard.h`): `wlr_keyboard_from_input_device`,
  `wlr_keyboard_set_keymap`/`set_repeat_info`, `wlr_keyboard_get_modifiers` →
  `WLR_MODIFIER_*` mask; `kb->events.key` (`wlr_keyboard_key_event{time_msec,
  keycode(evdev), state}`), `kb->events.modifiers` (data = the kb). Seat:
  `wlr_seat_set_keyboard`, `wlr_seat_keyboard_notify_key/modifiers/enter/clear_focus`.
- **xkb**: `xkb_state_key_get_syms`/`xkb_state_key_get_one_sym` (key = **evdev+8**),
  `xkb_keysym_to_lower`; keysyms `XKB_KEY_{Escape,Tab,Left,Right,space,q,1..4}`.
  Headless needs no device: a standalone `xkb_context`+`xkb_keymap_new_from_names(
  ctx,nullptr,…)`+`xkb_state_new` suffices for the offset unit test. All reachable
  via `toolkit/wlr.hpp` already (no new include).
- **Scene restacking** (`wlr_scene.h`): `wlr_scene_node_raise_to_top`,
  `wlr_scene_node_lower_to_bottom`, `wlr_scene_node_place_above/below`,
  `wlr_scene_node_set_enabled` (VERIFY exact names at impl time).
- **Timer**: `wl_event_loop_add_timer` / `wl_event_source_timer_update`
  (`wayland-server-core.h`, present).

---

## Phase A — data models + static toolbar/clock (→ REVIEW CHECKPOINT)

Each task is test-first, compile+test+commit green, coverage ≥80% throughout.

**A1 — `StackingList` + `StackEntity` (pure).** Port the 5-sentinel layered
stacking list verbatim from `reference/blackboxwm/src/StackingList.*`. Failing test
`tests/unit/stackinglist_test.cc`: construct → `empty()`, internal 5 sentinels;
insert/append/remove/raise/lower/changeLayer/front/back across ≥2 layers; the
invariant (5 sentinels survive every op) + edges (raise-at-top/lower-at-bottom
no-op, remove-of-layer-top). `StackEntity` abstract (`windowID()` → `View*` later).
Commit: `src+tests: port StackingList 5-sentinel layered model + L0 invariants (M4-A)`

**A2 — `Workspace` + `WorkspaceModel` (pure).** `WorkspaceModel(4)`: `count`,
`current`, `name(i)=="Workspace i+1"`, per-workspace focus memory; the
show-set/hide-set policy for a `switchTo(i)` (pure, no scene). `setCurrent` defined
but unused until B5. Commit: `src+tests: Workspace + WorkspaceModel pure model, default 4 ws (M4-A)`

**A3 — `bt::Clock` + `formatClock` (pure toolkit).** `toolkit/Clock.{hh,cc}`:
abstract `Clock` (`nowMs()`+`wallSeconds()`), `SystemClock`, `VirtualClock`
(`advance`/`setWall`), `formatClock(int64_t, "%I:%M %p")` via **`gmtime_r`**.
Failing test `tests/unit/clock_test.cc`: `formatClock(0)=="12:00 AM"`,
`formatClock(14*3600+5*60)=="02:05 PM"`, `VirtualClock` advance arithmetic, format
fallback. Commit: `toolkit+tests: bt::Clock/VirtualClock + gmtime_r formatClock seam (M4-A)`

**A4 — `bbai::Timer` + `TimerRegistry` (pure `fireDue`).** `src/Timer.{hh,cc}`:
`TimeoutHandler`, RAII recurring `Timer`, `TimerRegistry(bt::Clock&,
wl_event_loop* /*nullable*/)` with `add`/`remove`/`fireDue(now_ms)`. Failing test
`tests/unit/timer_test.cc` (loop=nullptr): recurring fires once per due interval +
re-arms; one-shot doesn't; RAII unregister; due-order. Production `wl_event_loop`
wiring lands in A6. Commit: `src+tests: bbai::Timer + TimerRegistry pure fireDue core (M4-A)`

**A5 — `Toolbar.geom.hh` (pure).** Constants (`kBarHeight=23`, `kLabelHeight=19`,
`kFrameMargin=2`, `kWidthPercent=66`, …) + `barRect(OW,OH)` and
`sectionRects(bar_w, label_w, clock_w, winlbl_w)` (the `extra`-subtraction tiling).
Widths come from a passed-in text-width callback (testable without fcft). Failing
test `tests/unit/toolbar_geom_test.cc`: `barRect(1280,720)=={218,697,844,23}`,
section tiling with a mock width fn. Do **not** hardcode `103`/`844` as compute
constants. Commit: `src+tests: Toolbar.geom.hh pure section-rect math (M4-A)`

**A6 — `Server` owns `Clock`+`TimerRegistry`; font-fit assert.** `Server` holds
`std::unique_ptr<bt::Clock> clock_` (headless→`VirtualClock(14:05:00 UTC)`,
else `SystemClock`) and `TimerRegistry` (production: one `wl_event_loop_add_timer`
re-armed to the next whole minute → `fireDue`). Test levers
`advanceClockForTest`/`wallSecondsForTest`/`clock()`/`timerRegistry()` next to the
`injectPointer*ForTest` block. Failing tests: `tests/unit/clock_seam_test.cc`
(advance → wall advanced, fake handler fired once) + add to `text_test.cc`
`CHECK(titleFont().height() <= frame::kLabelHeight)` (the real fit invariant, not
`==15`). Commit: `src+tests: Server owns Clock+TimerRegistry; assert title text fits the label (M4-A)`

**A7 — Toolbar chrome on `layer_top` (ticking).** `src/Toolbar.{hh,cc}`: owns a
`wlr_scene_tree` under `Server::layer_top`; renders workspace label (current name),
window label (focused title or blank), clock, and 4 **inert** arrow buttons via the
seam; tracks the clock node separately for cheap per-minute rebuild; implements
`TimeoutHandler::timeout()→redrawClock()` on a recurring 60s `Timer`. Server
constructs it and calls `redrawWindowLabel` on focus change. Failing test
`tests/system/toolbar_test.cc` (`env: text_env`): golden `m4-toolbar.png`
(`Workspace 1 | <blank> | 02:05 PM`), then `advanceClockForTest(60)` →
`m4-toolbar-tick.png` (`02:06 PM`) + `CHECK(formatClock(wallSecondsForTest())=="02:06 PM")`.
Commit: `src+tests: Toolbar top-layer chrome, ticking clock, golden + tick (M4-A)`

**A-CHECKPOINT**: M1–M3 goldens pass; coverage ≥80%; toolbar demoable with the
frozen-then-ticked clock. **Land + pause for review.**

---

## Phase B — keyboard/keybindings + root menu + workspace switching

**B1 — evdev→XKB `+8` pin (pure xkb).** `tests/unit/keycode_test.cc`: a real
`xkb_state` (no device) → `xkb_state_key_get_one_sym(state, KEY_TAB+8)==XKB_KEY_Tab`,
`KEY_ESC+8→Escape`, `KEY_1+8→1`. Tiny `bbai::evdevToXkb(kc)==kc+8` helper shared by
prod+test. Commit: `tests: pin evdev->XKB +8 offset + keysym lookup, pure xkb (M4-B)`

**B2 — `Keybindings` table + matcher (pure).** `src/Keybindings.{hh,cc}`:
`struct Binding{mods, sym, Action}`, the M4 defaults, `dispatch(mods, sym)` with
**equality-mask** (Super+Tab ≠ Super+Shift+Tab), CAPS/MOD2 stripped, lowercase
match. Failing test `tests/unit/keybinding_test.cc`. Commit: `src+tests: Keybindings table + equality-mask matcher (M4-B)`

**B3 — `Keyboard` wiring + `onKey` + `injectKeyForTest`.** `src/Keyboard.{hh,cc}`
(RAII key/modifiers/destroy listeners → `Server::onKey/onModifiers/removeKeyboard`);
replace the incomplete `new_input` keyboard branch (push a `Keyboard`,
`set_repeat_info(25,600)`). `onKey` does `+8` → keysym → mods → (menu check, B8) →
`keybindings_.dispatch` (swallow + `swallowed_keycodes_`) else
`wlr_seat_keyboard_notify_key`. `injectKeyForTest` calls the matcher directly.
Failing test `tests/system/keybinding_test.cc` (assert the action fired; wire the
side effect once B5 lands). Commit: `src+tests: Keyboard RAII + onKey funnel + deviceless injectKeyForTest (M4-B)`

**B4 — `View` is a `StackEntity`; scene restack.** `View : public StackEntity`
(`windowID()→this`); `Server` owns the single `StackingList`; `raiseView`/`lowerView`
→ `wlr_scene_node_raise_to_top`/`lower_to_bottom`; per-`View` `workspace_` +
`setOnWorkspace(bool)` → `wlr_scene_node_set_enabled`. Failing test
`tests/system/restack_test.cc` (two clients): raise flips the golden + scene order.
Commit: `src+tests: View is StackEntity; scene raise/lower + enable from model (M4-B)`

**B5 — Workspace switching.** `Server::setCurrentWorkspace(i)`: disable outgoing-ws
views, store outgoing focus, enable incoming, **restore/clear focus**, refresh
pointer focus (`onPointerMotion(nowMsec())`), `toolbar_->redrawWorkspaceLabel()`.
Wire `Mod4+Left/Right/1..4` + the toolbar arrows. Failing test
`tests/system/workspace_switch_test.cc` (two clients on two ws): enabled-flag +
golden + focus asserts (no hidden surface holds focus). Commit: `src+tests: setCurrentWorkspace show/hide + focus restore + label (M4-B)`

**B6 — `bbai::menu` geometry + `MenuItem` (pure).** `src/Menu.geom.hh` +
`src/MenuItem.hh` ported from `lib/Menu.cc` math (single-column). Failing test
`tests/unit/menu_geometry_test.cc` with a mock width fn (item rects, hit-test,
separators skipped). Commit: `src+tests: bbai::menu pure geometry + MenuItem model (M4-B)`

**B7 — `CommandRunner` exec seam.** `src/CommandRunner.{hh,cc}`: abstract
`run(std::vector<std::string> argv)`, `PosixCommandRunner` (double-fork/`setsid`/
`execvp`, sets `WAYLAND_DISPLAY`), `FakeCommandRunner` (records). Server holds a
`CommandRunner*` + `setCommandRunnerForTest`. Failing test
`tests/unit/command_runner_test.cc`. Commit: `src+tests: CommandRunner argv exec seam + recording fake (M4-B)`

**B8 — Root menu chrome + modal input + actions.** `src/Menu.{hh,cc}` (scene tree
on `layer_overlay`, hover re-emits affected items) + `src/Rootmenu.{hh,cc}`
(built-in tree). `Server`: `active_menu_`, `openRootMenu`/`closeMenus`, gate
`active_menu_ != nullptr` first in `onPointer*`/`onKey`, open on `BTN_RIGHT` over
**background only** (chrome-ancestry guard), focus hygiene on open/close. Actions:
`Exec`→`commandRunner().run`, `WorkspaceSwitch`→`setCurrentWorkspace`,
`New/RemoveWorkspace`→model resync, `Exit`→`wl_display_terminate`, `Restart` stub.
Immediate cascade on hover (no menu-delay timer). Failing tests:
`rootmenu_test.cc` (right-click opens golden, outside/Escape dismiss),
`menu_exec_test.cc` (`FakeCommandRunner` records argv), `menu_vs_grab_test.cc`
(`cursor_mode==Passthrough` while menu open; a right-click on the toolbar does NOT
open the menu). Commit: `src+tests: modal Rootmenu chrome on overlay + actions + dismiss (M4-B)`

---

## Key risks → de-risking test
| Risk | De-risked by |
|---|---|
| Key injector bypasses evdev→XKB seam | B1 pins `+8` + keysym lookup purely; B3 runs production `onKey` |
| Modal menu vs move/resize grab | B8 `menu_vs_grab_test` (`cursor_mode==Passthrough` while open; drag works after dismiss) |
| Focus left on a hidden surface after switch | B5 asserts restored focus + no hidden surface holds focus |
| Clock flake from wall-clock / TZ | A3 `gmtime_r` exact strings + A7 two-golden tick via `advanceClockForTest` |
| Sentinel off-by-one reorders Z | A1 invariants + B4 restack golden + scene-order assert |
| Geometry/text-fit drift | A6 asserts `titleFont().height() <= kLabelHeight`; geom tests use a mock width fn |
| Exec spawns real processes in CI | B7/B8 `FakeCommandRunner` — argv recorded, nothing spawned |
| Right-click on toolbar opens the menu | B8 asserts a toolbar right-click does NOT open + chrome guard |
| Toolbar label staleness on switch | B5 golden asserts "Workspace 2"; A7 asserts window-label re-render on focus |

## Acceptance criteria
**Phase A (checkpoint):** StackingList 5-sentinel invariant (L0); WorkspaceModel
(4 ws, names, focus memory, L0); `bt::Clock`/`VirtualClock`/`formatClock` (gmtime_r)
+ `Timer`/`TimerRegistry::fireDue` (L0); Toolbar on `layer_top` reading
`Workspace 1 | <blank> | 02:05 PM` with a tick to `02:06 PM` via the injected clock
(two goldens, no wall-clock); `titleFont().height() <= kLabelHeight` asserted;
M1–M3 goldens pass; coverage ≥80%; every commit green.

**Phase B (milestone):** real keyboard `onKey` funnel (evdev→XKB, keysym, mods,
swallow+release); Mod4 keybindings (workspace switch/by-number, menu, cycle, close)
equality-matched; workspace switching (enable/disable + focus restore + label);
modal root menu on `layer_overlay` (right-click background + Mod4+space, built-in
tree, hover/Escape/outside dismiss, modality vs grab); actions wired via injectable
`CommandRunner` (no real spawn), workspace/exit; right-click on chrome does NOT open
the menu; all M4 + M1–M3 goldens pass; coverage ≥80%; no wall-clock / sleep / real
spawn / GPU-device in any test.

**Out of M4 (deferred):** `.blackboxrc`/style/menu-file parsing + toolbar auto-hide
/ placement (M5); `ext-workspace-v1` export (M7); iconbar icon population (awaits
iconify); menu-delay timer; `Restart` self-exec; Configmenu/Slitmenu.
