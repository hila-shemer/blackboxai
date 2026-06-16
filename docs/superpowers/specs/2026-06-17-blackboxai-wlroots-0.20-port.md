# BlackboxAI — wlroots 0.20 Port + Deferred M4 Features

**Status:** approved (design); spec under review
**Date:** 2026-06-17
**Author:** Claude (Opus 4.8) with Hila Shemer
**Supersedes pin:** the wlroots 0.19 dependency from `2026-06-14-blackboxai-design.md` §1

---

## 1. Why this milestone exists

A Fedora 43 → 44 host upgrade replaced the system `wlroots` 0.19 with **0.20.1**. The project
hard-pins `wlroots-0.19` (`>=0.19.0, <0.20.0` — "ABI breaks every minor release", `meson.build:15`),
so `libwlroots-0.19.so` and `wlroots-0.19.pc` are gone and the build no longer links. Fedora 44
ships only wlroots **0.20.0 / 0.20.1**; 0.19 is unavailable from the distro and upstream. The code
itself is intact and was green (M1–M4, ~100 tests, 90% coverage) before the upgrade — **this is an
environment break, not a code regression.** CI is unaffected (it pins the `fedora:43` container,
which still has 0.19); the break is host-only.

We take the forced migration as the moment to also finish the four M4 features that were
intentionally deferred to a later milestone.

## 2. Goals & success criteria

- Clean build of `wayland-rewrite` against **wlroots 0.20.x** on Fedora 44 (the 0.19 path removed).
- All tests pass; coverage gate **≥80%** (`gcovr` over `toolkit/` + `src/`) holds.
- **Phase 1 (port): the existing golden PNGs still pass unchanged — no re-bless.** This is the
  equivalence oracle: under the harness's own compare (tolerance 2, budget 0) a behavior-preserving
  migration must not move a pixel beyond what already passes.
- **Phase 2 (features): goldens change only with intent**, re-blessed per feature with the diff
  attributable to that feature alone.
- CI green on a **fedora:44** container (bumped from fedora:43).

### Non-goals

- No 0.19 / 0.20 dual-support. Single-target **0.20.x**; the version gate becomes `>=0.20.0, <0.21.0`.
- No M5 work (drop-in `.blackboxrc`/style/menu-file parsing, slit geometry). Menu-*file* parsing is
  explicitly out even though cascade submenus are in — the submenu work uses the existing in-code
  menu model, not a parser.
- No unrelated refactoring beyond what the API migration forces.

## 3. Structure — two phases, one milestone

Both land on `wayland-rewrite`. Phase 2 stacks on a green Phase 1 so the port stays bisectable and
the equivalence oracle stays meaningful.

### Phase 1 — the port (behavior-preserving)

Re-pin and migrate the wlroots API surface with **zero intended behavior change**:

- `meson.build`: `dependency('wlroots-0.20', version: ['>=0.20.0','<0.21.0'])`; delete the 0.19 gate.
- Fix the hardcoded sanitize-shim path `wlr_incdir / 'wlroots-0.19' / 'wlr' / 'types' / 'wlr_scene.h'`
  (`meson.build:44`) to `wlroots-0.20`; re-verify `tools/sanitize-c-header.sh` against the 0.20
  `wlr_scene.h` / `fcft.h` (the C99 `[static N]` array-parameter syntax that C++ rejects).
- Migrate the **~125-symbol** wlroots surface across the ~20 files that touch it (`src/*`,
  `toolkit/wlr.hpp` central include shim, `tests/harness/HeadlessFixture.cc`, the wlr-touching tests).

### Phase 2 — the four deferred M4 features

Each its own TDD commit (tests first), stacked on the green port, with goldens re-blessed and the
pixel diff attributable to that feature:

1. **Active/inactive frame-focus swap** — frame decoration restyles on focus change across multiple
   windows (cut from M3; needs >1 window). Touches `View` / `Decoration`.
2. **Toolbar auto-hide + placements** — toolbar hides when unfocused; support placements beyond
   `BottomCenter`. Touches `Toolbar`.
3. **Cascade submenus** — nested root-menu submenus over the flat M4 menu, including the
   `NewWorkspace` / `RemoveWorkspace` actions already coded in `itemClicked` but unreachable from the
   flat menu. Touches `Menu` / `Rootmenu`. (Menu-*file* parsing stays out — M5.)
4. **Iconbar + iconify/maximize** — wire the iconify/maximize window-button actions (drawn in M3 but
   not wired) and populate the toolbar iconbar. Touches window-button actions + `Toolbar`/iconbar.

## 4. Phase 1 methodology — compiler-driven

The local 0.19 headers are gone (`/usr/include/wlroots-0.19` is an empty orphan dir, 0 headers), so
a header-to-header diff is impossible. The **authoritative delta is the compiler**, scoped to exactly
what the code uses: re-point to 0.20, build, and fix every error/warning until green.

A spot-check of the installed 0.20 headers shows the load-bearing APIs survive structurally —
`wlr_buffer_begin_data_ptr_access` / `wlr_buffer_end_data_ptr_access`, `wlr_output_commit_state`,
`wlr_output_state_init` / `_finish`, `wlr_scene_output_build_state`, `wlr_scene_buffer_create`,
`wlr_scene_node_at` — so the surface is bounded and the breaks are expected to be subtler (struct
fields, enum/signal renames, the custom `DataBuffer` impl-struct, output-commit details).

Work proceeds **one commit per coherent subsystem** — scene, buffer (`DataBuffer`), seat/input,
xdg-shell, output, decoration — each compiling, passing its tests, and goldens still passing
without re-bless before the next. No subsystem commit lands red.

## 5. Phase 2 design source

Per-feature design is produced by an **ultracode design-research workflow** against
`reference/blackboxwm` (parallel agents, one per feature, verifying the reference WM's behavior and
the available 0.20 API; a synthesizer emits a phased TDD breakdown). The detailed per-feature design
lands in the implementation plan, informed by that research — not invented here.

## 6. Verification

- `meson test -C build` — unit (L0 doctest) + system (headless + pixman) suites.
- `gcovr -r . build --filter 'toolkit/' --filter 'src/' --fail-under-line=80` — coverage gate.
- Golden compare (`compareGolden`, tolerance 2 / budget 0); `BLESS=1` only to re-bless Phase-2
  goldens with a recorded reason.
- Clean-room CI run on **fedora:44** (`.github/workflows/ci.yml` base image bumped; the unpinned
  `dnf install wlroots-devel` then resolves to 0.20.1).
- **Ultracode adversarial review before push** — the M3 and M4-B reviews each caught a real bug;
  worth the spend on a milestone this size.

## 7. Risks / watch-list

- **`wlr_scene.h` C++ shim.** The 0.20 header may have different `[static N]` sites than 0.19;
  re-run `sanitize-c-header.sh` and confirm `wlr_headers_test` still guards the extern-"C" inclusion.
- **`DataBuffer` custom buffer impl.** Changes to `struct wlr_buffer_impl` (function set, version
  field, or `data_ptr_access` flags/signature) are the classic 0.20 break point for a hand-rolled
  buffer; the `databuffer_test` is the canary.
- **Documented headless gotchas** (`wlr_headless_add_output`, teardown disconnect order, the
  `wlr_scene.h [static 4]` shim) — re-verify each still holds on 0.20; update the project memory
  (`blackboxai-wlroots-gotchas.md`) where it shifts.
- **Output-commit semantics.** `wlr_output_state` / `wlr_scene_output_build_state` survive by name;
  confirm the commit/atomic-test flow and damage handling are unchanged in behavior, not just shape.
- **Forward-only.** Once re-pinned there is no 0.19 fallback (0.19 is unavailable on fc44 regardless).
  The git history before this milestone remains the only 0.19-buildable reference.

## 8. References

- Architecture/roadmap: `docs/superpowers/specs/2026-06-14-blackboxai-design.md`.
- Prior milestone plans: `docs/superpowers/plans/2026-06-14-blackboxai-m{1,2,3,4}-*.md`.
- Deferred-item provenance + headless coverage gaps: `docs/superpowers/RESUME.md`.
- Reference WM: `reference/blackboxwm` @ `22c0762` (gitignored, not vendored into history).
- Project memory: `~/.claude/projects/-home-hila-proj-blackboxai/memory/` — esp.
  `blackboxai-wlroots-gotchas.md` (21 gotchas verified through M4, all against 0.19).
