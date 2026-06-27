# BlackboxAI M7 (slice 1) — Daily-drivable login session — Design

**Status:** approved (design); implementation pending
**Date:** 2026-06-27
**Branch:** `wayland-rewrite`

## 1. What this is

The first M7 slice: turn the compositor from "runs nested, runs once on a TTY
if you're lucky" into a session you can log into and stay in. Not all of M7 -
this is the minimal set that makes login *safe* and *useful*: keep the session
across VT-switches, fail loud instead of black-screen, light up every monitor,
autostart your usual agents, ship a `.desktop` so a display manager lists it,
and a key to quit a wedged session.

Everything that makes it *nice* rather than *livable* is deferred on purpose
(work-area/struts, move-to-output/snap, idle/lock, XWayland) - those are later
layers.

## 2. The failure modes we're closing

The nested smoke test proved the input funnel and compositing work. Three things
still make a real TTY login unsafe, all confirmed by code-read:

- **Session discarded.** `Server.cc:36` calls `wlr_backend_autocreate(loop,
  nullptr)` - no handle, no `active`/`inactive` hook. On VT-switch back the
  screen can come back blank/stale because nothing re-renders.
- **Silent black-screen hang.** `wlr_backend_start`'s return is ignored; `ok()`
  (`Server.hh:39`) only checks `display && backend` non-null. A backend that's
  created but fails to *start* (no DRM master / seat held by another session)
  leaves you in `wl_display_run` staring at black, no error.
- **One monitor.** `new_output` (`Server.cc:167`) keeps only the first head
  ("M1: a single output"). On a multi-head machine the rest stay dark.

Plus two gaps that aren't bugs but block daily use: nothing reads XDG autostart
(a fresh session comes up empty), and there's no `.desktop` so no DM lists it.

## 3. The design

### 3.1 Retain the session, honor VT-switch
Pass `&session_` to `wlr_backend_autocreate` and keep it. Listen on
`wlr_session.events.active`; on resume, schedule a frame on every output. The
DRM backend already handles the device pause/resume through libseat - the part
that's missing today is the re-render on the way back, which is why the screen
goes stale. No custom DRM-master code. (Exact 0.20 `wlr_session` signature gets a
compiled POC at plan time, per project habit.)

### 3.2 Fail loud
Check `wlr_backend_start`'s return. Store it; `ok()` becomes `display && backend
&& started`. On failure the constructor prints a clear stderr line naming the
likely cause (no seat, or DRM master held by another session), and `main.cc`'s
existing `!ok()` branch already returns 1 - so a dead backend exits instead of
hanging on black.

### 3.3 Light up every output
Drop the single-output guard in `new_output`: an `Output` per head, laid out
left-to-right through the existing `wlr_output_layout`. The first head enumerated
stays *primary* (`active_output`) and keeps the toolbar; the rest render
background and can host windows. The single-output assumptions that remain - toolbar placement, the
maximize work-area (`Server.cc:591`) - stay primary-bound on purpose; per-output
work-area is layer 4. Outcome: every monitor shows the desktop, window
management is still primary-centric. Honest stopgap, not the finished thing.

### 3.4 XDG autostart
On startup, read `~/.config/autostart/*.desktop` and `/etc/xdg/autostart/*.desktop`
(user shadows system by basename). Honor `Hidden=true` (skip), `TryExec` (skip if
not on `PATH`), `OnlyShowIn`/`NotShowIn` against our desktop id, strip `%`-field
codes from `Exec`, and spawn via the existing `CommandRunner`. We export
`XDG_CURRENT_DESKTOP=Blackbox` for the session so `OnlyShowIn` resolves.
Show-everywhere agents (polkit, ssh-agent, nm-applet) run; GNOME-only entries
correctly don't.

### 3.5 The `.desktop`
Ship `blackboxai.desktop` via meson `install_data` to
`$prefix/share/wayland-sessions/`. Minimal valid entry: `Name=BlackboxAI`,
`Exec=blackboxai`, `Type=Application`, `DesktopNames=Blackbox`. A DM then lists
it and starts the session with logind's seat already set up.

### 3.6 Escape hatch
Bind `Ctrl+Alt+Backspace` to the compositor-quit action - the same one the
menu's `[exit]` triggers (verify it calls `wl_display_terminate`; wire it if the
M4 entry is a stub). Today `Super+q` only closes the focused window, so a
half-wedged session is a trap. The classic key; trivially rebindable.

## 4. Testing

Per the chosen posture - automate what headless can reach, hand-verify the rest.

**Automated (CI / headless):**
- `ok()` returns false when the backend fails to start (drive a start failure;
  assert `ok()` false).
- XDG autostart `.desktop` parser: `Exec` field-code stripping,
  `Hidden`/`TryExec`/`OnlyShowIn`/`NotShowIn` filtering, user-shadows-system,
  malformed entries don't crash.
- Multi-output: N headless heads → N `Output`s in the layout, primary keeps the
  toolbar.
- `blackboxai.desktop` is well-formed (`desktop-file-validate` if present, else a
  parse check).

**Hand-verified on a TTY (the checklist lives in the plan):**
1. `Ctrl+Alt+F3`, log in, run `blackboxai` - desktop + toolbar on the primary,
   every head lit.
2. Autostart agents are running (tray icon / polkit prompt where expected).
3. `Ctrl+Alt+F1` to GNOME and back - screen returns, not blank.
4. `Ctrl+Alt+Backspace` cleanly exits to the login prompt.
5. Sabotage check: start with the seat held by another session - loud stderr
   error, not a black hang.
6. Only after all that: pick "BlackboxAI" at GDM.

## 5. Out of scope (later layers)
- Per-output work-area/struts, move-to-output, half-screen snap → layer 4.
- Idle/lock, DPMS policy.
- XWayland → layer 6 (decision pending).
- Interactive features (universal MRU alt-tab, fullscreen toggle) → their own
  layers.

## 6. References
- Audit anchors: `Server.cc:36` (session), `Server.cc:181` + `Server.hh:39`
  (start/ok), `Server.cc:167` (single output), `CommandRunner.cc` (spawn engine).
- Roadmap: `specs/2026-06-14-blackboxai-design.md` §5 (M7).
- Exact wlroots 0.20 session/DRM API verified at plan time (compiled POC, per
  project habit - see the wlroots-gotchas memory).
