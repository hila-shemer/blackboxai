# BlackboxAI — Resume / Handoff

_Last updated: 2026-06-14. This is the orientation doc for picking the project back
up in a fresh session. For the architecture, read
`docs/superpowers/specs/2026-06-14-blackboxai-design.md`; for per-milestone plans,
`docs/superpowers/plans/`; for hard-won gotchas + project context, the auto-memory
under `~/.claude/projects/-home-hila-proj-blackboxai/memory/`._

## Resume prompt (paste into a fresh session)

> Resume BlackboxAI (Wayland compositor rewrite of Blackbox, wlroots 0.19, C++20,
> at `/home/hila/proj/blackboxai`). Read `docs/superpowers/RESUME.md` first, then
> the project memory `~/.claude/projects/-home-hila-proj-blackboxai/memory/MEMORY.md`.
> Status: **M1–M4 complete and pushed.** M1–M3 + M4 **Phase A** are on
> `wayland-rewrite` (tip `ecdd4a7`); M4 **Phase B** (keyboard/keybindings +
> workspaces + modal root menu) is on the forked branch **`m4-phase-b`** (tip
> `87297dc`), **not yet merged**. Both branches are green (89% coverage, gate 80%,
> clean-room CI-verified). My working style: pause to ask the genuinely important
> user-owned decisions, then go all the way to push-ready with maximum effort;
> rewrite over patch; tests + golden-PNGs over caveats; keep coverage up; use the
> ultracode multi-agent treatment (parallel design research up front + an
> adversarial review before pushing) for substantial milestones; for big
> milestones offer a mid-way review checkpoint. **Next options (ask me which):**
> (a) merge `m4-phase-b` → `wayland-rewrite` + add the `milestone: M4 complete`
> marker commit (M1–M3 each have one; M4 does not yet); (b) start the **M5** plan
> (style/config fidelity: drop-in `.blackboxrc`/style/menu-file parsing + slit
> geometry); (c) knock out the deferred M4 follow-ups. Don't start coding before
> confirming which, and surface any important decision first.

---

## Where things stand

| Milestone | What | Branch / tip | Pushed |
|---|---|---|---|
| **M1** | headless textured background (renderer seam + golden harness) | `wayland-rewrite` | ✅ |
| **M2** | real xdg-shell client mapped & composited | `wayland-rewrite` | ✅ |
| **M3** | Blackbox SSD decoration frame + move/resize + fcft title text | `wayland-rewrite` | ✅ |
| **M4 Phase A** | StackingList/Workspace models + `bt::Clock`/Timer + Toolbar w/ ticking clock | `wayland-rewrite` @ `ecdd4a7` | ✅ |
| **M4 Phase B** | Keyboard/keybindings + workspace switching + modal Rootmenu + CommandRunner | `m4-phase-b` @ `87297dc` (forked from `ecdd4a7`) | ✅ |

- Remote: `github.com:hila-shemer/blackboxai`. `main` is the untouched base (`90d2b70`).
- **`m4-phase-b` is NOT merged into `wayland-rewrite`.** PR link offered on push:
  `https://github.com/hila-shemer/blackboxai/pull/new/m4-phase-b`.
- **No `milestone: M4 complete` marker commit yet** (M1–M3 each have an empty
  `milestone: …` marker; M4 should get one after the merge).
- 93 test cases, **89% line coverage** (gate 80%). Working tree clean.

## Build / test / verify (the exact commands)

```sh
cd /home/hila/proj/blackboxai
meson setup build -Db_coverage=true -Dbuildtype=debug && ninja -C build
meson test -C build                      # all suites (unit + system)
meson test -C build --suite unit         # L0 pure tests (doctest)
export XDG_RUNTIME_DIR=$(mktemp -d); chmod 700 "$XDG_RUNTIME_DIR"   # system suite needs this
WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build --suite system
gcovr -r . build --filter 'toolkit/' --filter 'src/' --fail-under-line=80   # coverage gate
```

- **Golden PNGs**: regenerate with `BLESS=1 … <test-binary>` (or `BLESS=1 meson test`).
  On mismatch the harness writes `<golden>-actual.png` / `-diff.png`.
- **Deterministic text**: any test that renders text / captures a text golden runs
  under an isolated fontconfig (`text_env` in `tests/meson.build`: `FONTCONFIG_FILE`
  → `tests/fixtures/fonts.conf`, a bundled LiberationMono, hinting off, `workdir` =
  source root). The menu/toolbar geometry depends on `font->height()` (18 isolated
  vs 22 host) — running a menu test WITHOUT that env silently clicks the wrong row.
- **Clean-room check** (mimics CI): fresh `meson setup /tmp/x …` + the suites +
  gcovr. CI is `.github/workflows/ci.yml` (fedora:43, installs `fcft-devel` etc.).

## How this milestone was run (the process the user likes)

1. Orient in the spec + reference, then **ask the genuinely user-owned decisions**
   via AskUserQuestion (scope/staging, deferred sub-decisions) — not conventional
   defaults.
2. **Ultracode design-research workflow** (parallel agents verify the real wlroots/
   library APIs + reference internals, then a synthesizer produces a consistency
   critique + phased TDD task breakdown). Output split to `/tmp/m{3,4}docs/`.
3. Write the milestone plan doc (`docs/superpowers/plans/…`), commit it.
4. Implement **TDD, one task per commit**, each compile+test+commit-green; goldens
   blessed then asserted strict. Big milestones get a **mid-way review checkpoint**.
5. Before pushing, an **ultracode adversarial-review workflow** (reviewers per
   dimension → each finding adversarially verified → only confirmed issues). Fix
   confirmed findings + add the missing tests, then push.
   - M3 review caught a real resize-anchor bug; M4-B review caught a null-`xkb_state`
     deref crash + a grab-stranding bug. Worth doing.

## Next work (pick one; confirm before coding)

- **Merge + milestone marker.** `git checkout wayland-rewrite && git merge --no-ff
  m4-phase-b` (or open the PR), then an empty `git commit --allow-empty -m
  "milestone: M4 … complete"` marker (matching M1–M3), then push.
- **M5 — style/config fidelity (drop-in) + slit geometry** (~2.5 wk per roadmap):
  load original `.blackboxrc`/style/menu files unchanged; live re-theme via
  Configmenu; slit placement/auto-hide. This is where most of the deferred M4
  items naturally land.
- **Deferred M4 follow-ups** (all intentionally cut to M5, flagged in code):
  - Menu **cascade submenus** (M4 ships a FLAT root menu) + menu-file parsing.
  - Toolbar **auto-hide** + placements other than BottomCenter.
  - **Active/inactive frame-focus swap** (needs multiple windows; M3 cut it).
  - **Iconbar** population (needs iconify state/actions; window buttons are drawn
    but iconify/maximize are not wired).
  - `NewWorkspace`/`RemoveWorkspace` menu actions are coded in `itemClicked` but
    not emitted by the flat menu yet (reachable when the Workspaces submenu lands).

## Documented coverage gaps (untestable headless — don't chase blindly)

- The `onKey`/`onModifiers` **handler bodies** (xkb sym lookup, the keycode-based
  `swallowed_keycodes_` swallow-release logic, forward-to-client) and the
  `new_input` keyboard branch. Headless fires no keyboard device, and wlroots 0.19
  exposes no public `wlr_keyboard_init` — constructing a standalone `wlr_keyboard`
  to drive `onKey` would be exactly the heavy mock/emulation infra this project
  avoids. What IS covered: the keysym-level binding matcher + actions
  (`keybinding_test`), the `evdev+8`→keysym seam purely (`keycode_test`), and the
  modal key-nav via `injectKeyForTest` (`rootmenu_test`). (Review finding #11:
  acknowledged, scope narrowed — not chased with a `wlr_keyboard` fake.)
- The `onModifiers` early-return + the `closeMenus` modifier re-sync
  (`wlr_seat_keyboard_notify_modifiers`) are real-device only: headless leaves the
  seat keyboard null, so the re-sync is a no-op there. The fix (review #2) is still
  in — a released modifier must not stay stuck-down in a client after a modal menu.
- The hot-plug keyboard focus push in `new_input` (review #9) — same real-device
  reason; no headless keyboard hot-plug.
- `viewForHandle`'s not-found return is defensive and unreachable via the public
  API: `removeView` calls `workspaces_.clearFocused(view)` before destroying a
  View, so no workspace can hand back a stale non-null handle (review #13).
- The production `wl_event_loop` timer firing path (`TimerRegistry` arms a real
  source; tests drive `fireDue` via the VirtualClock). Pre-existing from Phase A.
- `PosixCommandRunner` IS now covered by a real harmless spawn test (touches a
  temp file).

Not a gap, but worth recording: a `wl_pointer.button` RELEASE for a button the
seat never recorded as pressed is dropped by `wlr_seat` itself — so a menu
dismissal cannot deliver an "orphan" release to a client (review #8 was confirmed
by code-reading but is benign at the wlroots layer; proven by `menu_action_test`'s
matched-pair-vs-dismiss button counts). No compositor-side swallow needed.

## Key references

- Spec: `docs/superpowers/specs/2026-06-14-blackboxai-design.md` (roadmap §5,
  renderer seam §2, module layout §3.4, test pyramid §4).
- Plans: `docs/superpowers/plans/2026-06-14-blackboxai-m{1,2,3,4}-*.md`.
- Reference WM source (gitignored, NOT vendored into history):
  `reference/blackboxwm` @ `22c0762` — port individual files with attribution.
- Memory (read at session start): `~/.claude/projects/-home-hila-proj-blackboxai/memory/`
  — `MEMORY.md` (index), `blackboxai-overview.md`, `blackboxai-wlroots-gotchas.md`
  (21 verified gotchas through M4), `blackboxai-test-discipline.md`.
