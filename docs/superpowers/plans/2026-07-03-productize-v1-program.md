# Productize v1 — Program Plan (wave 1)

> Spec: `docs/superpowers/specs/2026-07-03-productize-v1-program-design.md`. Per-slice
> implementation plans: `2026-07-03-{sni-core,lock-idle,work-area,rc-style,menu-wire}.md`.
> This doc pins what the slices share: decisions, seams, landing order, re-bless policy.

## Decisions (locked 2026-07-03)

User-owned, asked and answered:
- **dbusmenu**: a `com.canonical.dbusmenu` client goes into the wave-2 *menus* slice,
  rendered in our own `bt::Menu`. Acceptance #2 stays honest; sni-core carries the menu
  path in its item model and nothing else.
- **rootCommand**: style-file rootCommand is never exec'd (theme-supplied shell - refused).
  `bsetroot -solid/-mod/-gradient` is interpreted natively so themed backgrounds stay
  faithful. rc-file rootCommand (user-authored) runs via `/bin/sh`. The asymmetry is the
  point.
- **[restart]**: classic-shaped exec-restart ships, documented plainly: on Wayland the
  compositor is the display server, restart kills every client.
- **Lock vs quit key**: `Ctrl+Alt+Backspace` is suppressed while locked. Lock means lock;
  the wedged-locker escape is a VT switch, which stays kernel-side.

Plan-author calls (rationale one line each):
- **send_locked timing**: after the first post-blank commit on every output + a fallback
  timer - the spec-correct variant; the cheap variant loses the suspend race.
- **Default style: Results**, not upstream's Gray - Gray chains first boot onto wildcard
  Xrm matching AND Parent_Relative pixel-copy. Both still land in-slice (5/19 shipped
  styles need wildcards; PR gets a flat-black fallback rung if the copy slips), but the
  default must not depend on the two riskiest tasks.
- **Fallback menu** (missing/empty menu file): keep the richer in-code menu - a daily
  driver wants terminal+run+workspaces on first boot, not classic's xterm/Restart/Exit.
- **Auto-hide strut**: classic 2px sliver - hidden toolbar still reserves its trigger edge.
- **Pipe menus** (`|cmd`): skip-with-diagnostic, explicit v1 non-goal - acceptance §6.1
  reads "menu files load unchanged, pipe-menu entries excepted (diagnosed, not silent)".
- **strftimeFormat**: parse-only in wave 1; the clock rendering swap lands with wave-2
  configmenu (keeps the pinned clock goldens from churning twice).
- **Slit pre-parse**: approved - rc-style parses `slit.*` keys + the slit texture now, so
  the wave-2 slit slice never opens `Config.cc` or the Style parser.

## Ownership pins (both-claimed items, resolved)

- `Toolbar::setPlacement`/`setAutoHide` promotion + Strut registration: **work-area owns**;
  rc-style only calls them when applying config.
- `wlr_output_create_global` (one line in the Output ctor): **lock-idle authors** (its
  hard requirement); work-area rebases over it.
- `src/Config.*`: **rc-style only**, whole wave. menu-wire reads `config().menuFile`.
- `bt::expandTilde` in new `toolkit/Util.{hh,cc}`: rc-style introduces; menu-wire rebases.
- Seam signatures: the synthesis contracts are law; a slice that wants to deviate renegotiates
  here, not in its worktree.

## Landing order (merge train)

Slices develop in parallel from day one - only landing is ordered:

1. **sni-core** - zero seams, ~6 Server lines, settles the CI dep change first.
2. **lock-idle** - flips on the compositor-wide `wl_output` global; land it isolated so any
   client-behavior shift is diagnosed alone, not smeared into the re-bless.
3. **work-area** - behavior-neutral refactor; its byte-identical-goldens guarantee is only
   checkable against today's constants, i.e. before rc-style moves the metrics.
4. **rc-style** - biggest slice lands with all providers in place; performs the ONE
   coordinated global golden re-bless of the wave, single commit.
5. **menu-wire** - consumes real seams instead of stubs; rootmenu goldens blessed exactly
   once against final metrics.

**Preflight (before any slice starts):** fresh/reconfigured build dir (`build/` is
currently stale - meson reconfigure fails until then), re-run the lock-idle and sni-core
POCs against the Fedora host headers (two scouts verified against the upstream 0.20.1
tarball on a wlroots-less box - same version, but re-cite from `/usr/include`), and fix
the 0.19→0.20 staleness in RESUME.md + project memory in passing.

## Parked defects (found by scouts, not wave-1 work)

- No cursor axis handler - scroll never reaches clients. Real daily-driver bug; parked to
  **wave-2 window-mgmt** explicitly so it doesn't fall through.
- `Menu::show` clamps to the primary output on multi-head - parked to **wave-2 menus**.

## Research artifacts

Scout + synthesis JSON (session scratchpad, ephemeral):
`/tmp/claude-1000/-home-hila-proj-blackboxai/8b5e93a4-d1a9-4374-9185-6a529349f965/scratchpad/wave1-research/`.
Everything load-bearing from them is baked into the slice plans; the scratchpad copies are
for this session's convenience only.
