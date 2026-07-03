# BlackboxAI — Productize v1 — Program Design

**Status:** approved (program shape + slice boundaries; per-slice design happens at each
slice's plan time, compiled-POC habit unchanged)
**Date:** 2026-07-03
**Branch:** `wayland-rewrite`

## 1. What this is

The remaining distance to the north star - a daily-drivable, nice-looking, drop-in-compatible
Blackbox on Wayland - executed as one program instead of serial milestones. The mechanisms are
built (M1-M4, login slice, alt-tab, screenshot); what's left is wiring real config into them,
the slit, session polish, and the product shell. The subsystems left are largely independent
by file territory, so they develop in parallel worktrees and integrate through a merge train.

## 2. Locked decisions (user, 2026-07-03)

- **Full product sweep** - all remaining code plus RPM spec (COPR-ready), README with real
  screenshots, man page, install/GDM docs.
- **Pure Wayland v1** - XWayland deferred entirely. Revisit after v1; not a build option yet.
- **Idle/lock = protocol support only** - `ext-session-lock-v1` + `ext-idle-notify-v1` so
  swaylock/swayidle Just Work. We do not own locker code.
- **One mid-way checkpoint** - after waves 1-2 land (M5 fidelity complete + work-area), the
  user hand-checks on a TTY. Everything after runs to push-ready.

## 3. Slices and waves

Slice boundaries are drawn on file ownership - the merge train only has to think at the seams.

### Wave 1 (parallel, independent territories)

| Slice | What | Owns |
|---|---|---|
| **rc-style** | Wire `Config` into `Server` (the hardcoded style at `Server.cc:108` dies); load original style files unchanged; ship `data/styles/`; knobs live: `focusNewWindows`, toolbar placement/auto-hide | `Server` style plumbing, `Config`, `data/` |
| **menu-wire** | `MenuParser` → live Rootmenu; `[include]`; restart/exit semantics | `Rootmenu` |
| **work-area** | Per-output work area; toolbar registers a strut; maximize consumes it | `Output`, geometry |
| **lock-idle** | `ext-session-lock-v1` + `ext-idle-notify-v1` | new files |
| **sni-core** | sd-bus StatusNotifierWatcher/Host + item model + mock publisher; no rendering | new files (D-Bus only) |

### Wave 2 (needs wave-1 seams)

| Slice | Needs |
|---|---|
| **configmenu** - Configmenu + live re-theme | rc-style |
| **slit** - slit geometry + SNI icon rendering + activate/context-menu proxy | work-area + sni-core |
| **window-mgmt** - fullscreen toggle, half-screen snap, move-to-output | work-area |
| **menus** - Windowmenu / Toolbarmenu / Slitmenu | rc-style |

**→ CHECKPOINT** after waves 1-2: "it's Blackbox on your config". TTY hand-check list lives
in the plan.

### Wave 3 (product shell)

ext-workspace-v1 export; RPM spec; README + screenshots (demo harness generates them); man
page ported from `reference/blackboxwm`; install/GDM docs. Program-wide adversarial review,
demos regenerated, push + park.

## 4. Integration discipline

- Each slice: design-research with compiled POCs first (wlroots-gotchas habit), TDD
  one-task-per-commit, golden PNGs, adversarial review *before* its merge-train entry.
- Merge train lands one slice at a time into a staging ref; gate = build + unit + system +
  coverage ≥80% (target stays ~90%). Bounce on red, resolve only at genuine seams.
- `wayland-rewrite` stays bisectable - every landed slice leaves it green.

## 5. Out of scope

XWayland (deferred, decision recorded above), a built-in locker, per-client CSD forcing,
XEMBED dockapps (non-goal since the original spec), Wayland protocols nobody asked for.

## 6. Acceptance

1. Original `.blackboxrc` + style + menu files load unchanged; live re-theme works.
2. Tray icons (real SNI apps) appear in the slit; activate + context menu work.
3. swaylock locks the session; idle notify fires.
4. Fullscreen/snap/move-to-output on multi-head with correct work-areas.
5. `dnf install` from the built RPM → pick BlackboxAI at GDM → full session, all heads,
   autostart up, VT-switch survives - the M7 hand-check extended.
6. CI green, coverage gate holds, demos regenerate.
