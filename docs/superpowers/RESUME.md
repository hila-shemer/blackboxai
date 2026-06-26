# BlackboxAI — Resume / Handoff

_Last updated: 2026-06-14. This is the orientation doc for picking the project back
up in a fresh session. For the architecture, read
`docs/superpowers/specs/2026-06-14-blackboxai-design.md`; for per-milestone plans,
`docs/superpowers/plans/`; for hard-won gotchas + project context, the auto-memory
under `~/.claude/projects/-home-hila-proj-blackboxai/memory/`._

## Resume prompt (paste into a fresh session)

> Resume BlackboxAI (Wayland compositor rewrite of Blackbox, wlroots **0.20**, C++20,
> at `/home/hila/proj/blackboxai`). Read `docs/superpowers/RESUME.md` first, then
> the project memory `~/.claude/projects/-home-hila-proj-blackboxai/memory/MEMORY.md`.
> Status: **M1–M4, the wlroots 0.19→0.20 port, AND all four deferred M4 features are
> complete and green on `wayland-rewrite`.** The fc43→44 host upgrade forced the 0.20
> port (build-system only — a sanitize shim for the new `render/color.h`; no src/ call
> site changed); CI bumped to fedora:44. The four deferred follow-ups are now DONE:
> active/inactive focus-swap, cascade root-menu submenus (Workspaces + New/Remove),
> iconbar + iconify/maximize (icon menu via middle-click + **Mod4+Alt+T**; root menu was
> already on Mod4+Space), toolbar auto-hide + placements (default off). An ultracode
> adversarial review passed (1 must-fix — cross-workspace deiconify focusing an invisible
> window — fixed; 2 auto-hide test-coverage gaps closed). Parked on the fork via git-park:
> `refs/wip/wayland-rewrite` (pre-demo checkpoint) and `refs/staging/wayland-rewrite`
> (full demos). Demo videos in `demos/*.mp4` (regenerate with `tools/make-demos.sh`;
> gitignored, on-disk artifacts). Green: **90% coverage**, gate 80%, 33 test executables
> incl. a `demo` suite. My working style: pause to ask the genuinely important user-owned
> decisions, then go all the way to push-ready with maximum effort; rewrite over patch;
> tests + golden-PNGs over caveats; keep coverage up; ultracode multi-agent treatment
> (parallel design research up front + adversarial review before pushing) for substantial
> milestones; offer a mid-way checkpoint. **Next: M5 — style/config fidelity** (drop-in
> `.blackboxrc`/style/menu-file parsing + live re-theme via Configmenu + slit geometry).
> The Configmenu is also where the deferred config knobs land (`focusNewWindows`, toolbar
> placement/auto-hide defaults — all hardcoded for now). Confirm scope before coding.

---

## Where things stand

| Milestone | What | Branch / tip | Pushed |
|---|---|---|---|
| **M1** | headless textured background (renderer seam + golden harness) | `wayland-rewrite` | ✅ |
| **M2** | real xdg-shell client mapped & composited | `wayland-rewrite` | ✅ |
| **M3** | Blackbox SSD decoration frame + move/resize + fcft title text | `wayland-rewrite` | ✅ |
| **M4 Phase A** | StackingList/Workspace models + `bt::Clock`/Timer + Toolbar w/ ticking clock | `wayland-rewrite` @ `ecdd4a7` | ✅ |
| **M4 Phase B** | Keyboard/keybindings + workspace switching + modal Rootmenu + CommandRunner | merged into `wayland-rewrite` @ `db5d242` | ✅ |
| **M4 complete** | second adversarial-review fixes + `milestone:` marker | `wayland-rewrite` @ `5e765a4` | ✅ |
| **M5(port)** | wlroots 0.19→0.20 (build-system shim for `render/color.h`) + CI fedora:44 | `wayland-rewrite` @ `b45727c`,`2d1ed83` | ✅ |
| **F1 focus-swap** | active/inactive frame decoration on focus | `wayland-rewrite` @ `ed74783` | ✅ |
| **F3 cascade submenus** | Workspaces submenu; New/Remove reachable | `wayland-rewrite` @ `69c239c` | ✅ |
| **F4 iconbar+iconify** | iconify/maximize/close wiring + Iconified-Windows menu | `wayland-rewrite` @ `dccb160` | ✅ |
| **F2 toolbar auto-hide** | placements + auto-hide (default OFF) | `wayland-rewrite` @ `527fcbe` | ✅ |
| **review + demos** | adversarial-review fixes + demo-video suite | `wayland-rewrite` @ `1c3ed6a` | ✅ wip+staging |
| **Screenshot** | Super+F7 region → clipboard PNG (capture + server-side `wlr_data_source`); 7 TDD + 4 review commits; GL-validated | `wayland-rewrite` @ `e0d10ea` | ✅ |

- Remote: `github.com:hila-shemer/blackboxai`. `main` is the untouched base (`90d2b70`).
- **Parked via git-park** (`/home/hila/remote-local-bin/git-park`, pushes hidden refs to
  the fork): `refs/wip/wayland-rewrite` (pre-demo checkpoint) and
  `refs/staging/wayland-rewrite` (full demos). `git-park list wip staging` to see them.
- **M4 Phase B IS merged into `wayland-rewrite`** (`--no-ff` merge `db5d242`, marker
  `5e765a4`). The `m4-phase-b` / `m4-phase-4-cont` refs (tip `ec9034d`) are kept but
  subsumed by the merge.
- M1–M4 each have a `milestone: …` marker commit.
- 33 test executables (unit / system / `demo` suites), **90% line coverage** (gate 80%).
  Working tree clean. Demo videos: `demos/*.mp4` (regenerate: `tools/make-demos.sh`).

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

## Screenshot feature (2026-06-26, DONE)

User-requested standalone feature, not part of the M-roadmap: **Super+F7 (Mod4+F7) →
click-drag a region → PNG straight to the clipboard** (GNOME-style dim overlay).
Whole-window/screen modes and file-save are explicit non-goals. Spec
`specs/2026-06-23-screenshot-region-clipboard-design.md`, plan
`plans/2026-06-26-screenshot-region-clipboard.md`. New: `src/Screenshot.{geom.hh,hh,cc}`
(pure `dimRects`, libpng `encodePng`, renderer-agnostic `captureRegion` via
`wlr_texture_read_pixels`) + `src/ClipboardImage.{hh,cc}` (server-side `wlr_data_source`,
async event-loop writer). `Server` gained `CursorMode::ScreenshotSelect`. Full ultracode
treatment: design-research workflow (compiled C POCs), TDD one-commit-per-task, adversarial
review (16 confirmed findings → 3 production fixes + hygiene + test hardening), nested GL
smoke run (`read_pixels(ARGB8888)` validated on the real GPU). 38 suites, 90% coverage.
The **only un-automated check**: a real cross-client paste (drag Super+F7, paste into an
app) — needs physical input; the serving half is covered by pipe tests.

## Next work (pick one; confirm before coding)

- _(done 2026-06-17..19: wlroots 0.20 port + ALL four deferred M4 follow-ups —
  active/inactive focus-swap, cascade submenus (+ New/RemoveWorkspace reachable),
  iconbar + iconify/maximize, toolbar auto-hide + placements — implemented subagent-
  driven with an adversarial review; parked wip + staging. See the status table.)_
- **M5 — style/config fidelity (drop-in) + slit geometry** (~2.5 wk per roadmap):
  load original `.blackboxrc`/style/menu files unchanged; live re-theme via Configmenu;
  slit placement/auto-hide. The deferred M4 *behaviors* are all done; what M5 still owns:
  - **menu-file parsing** (the cascade machinery exists; M4/Phase-2 used the in-code
    menu — drop-in menu files are M5).
  - the **Configmenu** + `.blackboxrc` parsing, which is where the currently-hardcoded
    knobs become configurable: `focusNewWindows` (Phase 2 hardcoded NO auto-focus, so a
    lone window renders inactive), toolbar **placement** (TopLeft..BottomRight all work
    via `setPlacementForTest`, no config source yet) and **auto-hide default** (OFF).
  - a real **work-area/strut** abstraction (maximize currently hardcodes output-minus-
    toolbar on the single M1 output).
  - the **slit** (SNI/StatusNotifierItem tray) — still unstarted.

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
