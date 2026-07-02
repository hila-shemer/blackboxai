# BlackboxAI — Universal MRU alt-tab — Design

**Status:** approved (design); implementation pending
**Date:** 2026-06-28
**Branch:** `wayland-rewrite`

## 1. What this is

Windows-style alt-tab: one flat list of all **visible** windows across all
workspaces, in most-recently-used order. Hold Alt, `Tab` forward / `Shift+Tab`
back; each candidate raises+focuses as a live preview; release Alt to commit,
`Escape` to cancel. No on-screen switcher. Replaces the `CycleNext`/`CyclePrev`
stubs (`Server.cc:706-707`) — currently bound to `Super+Tab`, currently no-ops.

## 2. Locked decisions

- Bind **`Alt+Tab`** (forward) / **`Alt+Shift+Tab`** (back); keep the existing
  `Super+Tab` / `Super+Shift+Tab` as aliases.
- **All workspaces** — committing on a window on another workspace switches to it.
- **MRU order** (last-used), not z-order.
- **No overlay** — the preview *is* the real window raising+focusing.
- **Ring = visible windows only.** Minimized windows are reached via the existing
  iconbar / icon-menu, never alt-tab — so there's no "skip minimized" special
  case; they're simply never in the ring.
- **`Escape` cancels** (restore focus to the session-start window). Perfect
  *stacking* restore on cancel is out for v1 — cancel restores focus; windows
  previewed mid-cycle may stay raised.

## 3. Design

### 3.1 The MRU list
New `std::vector<View*> mru_` in `Server`, most-recent first. `focusView(v)` moves
`v` to the front; `removeView(v)` erases it. Independent of the z-order
`StackingList`. This is the single source of "last-used order."

### 3.2 The cycle session — modal while the modifier is held
Alt-tab is modal while Alt is down, so `Server` gains: `bool cycling_`, a frozen
`std::vector<View*> cycle_ring_`, `size_t cycle_index_`, and `View *cycle_start_`
(for cancel).

- **Start** (first `CycleNext`/`Prev`): build `cycle_ring_` = the visible windows
  in `mru_` order; save `cycle_start_ = focused_view`; step one from the front
  (index 1, the next-most-recent); preview. With 0 or 1 visible windows the
  session is a no-op — nothing to cycle to.
- **Step** (subsequent, while held): advance/retreat `cycle_index_` with
  wraparound over the frozen ring; preview.
- **Preview** = `raiseView` + focus of `ring[index]` with the **MRU update
  suppressed**. `focusView` normally moves its target to the `mru_` front (§3.1),
  which would scramble the frozen ring on every Tab — so the cycle focuses via a
  suppressed path (`focusView(v, /*update_mru=*/false)`, or a dedicated
  preview-focus). Only **commit** reorders `mru_`.
- **Commit** (modifier released → `onModifiers`): the previewed window is already
  focused; now move it to the `mru_` front and clear `cycling_`. If it's on
  another workspace, `setCurrentWorkspace` to it.
- **Cancel** (`Escape` while cycling): `focusView(cycle_start_)`; clear `cycling_`.

### 3.3 Modifier-release detection
The commit trigger is "the modifier that *started* the cycle went up."
`onModifiers` already runs on every modifier change; while `cycling_`, when the
starting modifier (Alt, or Super for the alias) is no longer held, commit. Track
which modifier opened the session so releasing *that one* commits — not any
incidental modifier change.

### 3.4 Escape while cycling
While `cycling_`, `Escape` cancels and is consumed (not forwarded to the client),
mirroring the modal root-menu's `Escape` handling.

### 3.5 Bindings
Wire `CycleNext → cycleStep(+1)`, `CyclePrev → cycleStep(-1)` in `executeAction`.
Add to the `Keybindings` table: `{ALT, Tab} → CycleNext`,
`{ALT|SHIFT, Tab / ISO_Left_Tab} → CyclePrev`; keep the existing `Super+Tab` /
`Super+Shift+Tab` rows as aliases.

## 4. Testing

- **Pure `Mru` helper** (`src/Mru.{hh,cc}`): move-to-front, erase, snapshot,
  next/prev with wraparound. Fully unit-tested headless — the ordering + cycle
  math with zero wlroots.
- **Session state machine** (system test via injected seams `cycleForTest(dir)`,
  `commitCycleForTest()`, `cancelCycleForTest()`): assert focus + `mru_` order +
  restack after start / step / commit / cancel, across two workspaces, and that a
  preview does not reorder `mru_` until commit.
- **Hand-verified only:** the *real* Alt-release edge in `onModifiers` — the
  keyboard-funnel gap the project already documents. The commit logic it calls is
  covered through the seam; only the live "starting modifier went up" event is
  device-only.

## 5. Out of scope (later / other layers)
- On-screen switcher overlay — revisit if the no-overlay flow feels lacking.
- Minimized windows in the ring — the iconbar owns restore.
- Perfect stacking restore on cancel.
- Per-application grouping — flat list only, by decision.

## 6. References
- Stubs replaced: `CycleNext`/`CyclePrev` at `Server.cc:706-707`; bindings at
  `Keybindings.cc:18-20`.
- Focus path: `Server::focusView` / `focused_view`; the z-order `StackingList`
  is distinct from `mru_`.
- Minimized restore (out of the ring): F4 iconbar — `deiconifyView`,
  `buildIconMenu`.
