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

## Carried items (owed by later waves, flagged by the plan writers)

- Wave-3 man page owes the **restart-kills-clients** caveat (documented at the exec site
  in main.cc by menu-wire; the user-facing doc is wave-3's).
- TTY checkpoint list gains: **nm-applet-class tray items are activate-only until the
  wave-2 dbusmenu client lands** - expected, not a wave-1 defect.
- Implementation happens in the `blackboxai-ci:f44` container (this box has no wlroots);
  plans' "host header re-cite" tasks re-cite from the container's /usr/include.

## TTY checkpoint hand-check list (accumulating)

From lock-idle (headless can't reach these): real swaylock locks/unlocks (proves key
delivery to a lock surface); real swayidle timeout fires; VT-switch while locked comes
back locked; locked-frame-presented-before-`locked` ordering against real vblank. From
sni-core: nm-applet-class items are activate-only until wave-2 dbusmenu (expected).

## Train log

- Stop 1 `sni-core` (see git log): clean merge, gate 39/39, 90%.
- Stop 2 `lock-idle` (`d9672ec`): 2 Server.cc conflicts, git-mediate resolved; gate
  42/42, 90%; wl_output global landed with ZERO golden churn as promised.
- Stop 3 `work-area` (`c1f3cca`): 1 tests/meson.build conflict; gate 44/44, 90%;
  goldens byte-identical - the slice's guarantee, provable only at this slot.
- Watch item: `lock_interactions` failed ONCE on a cold-cache first parallel run
  (`REQUIRE(lc.ok())` - client connect under load); 10x single + 3x full suite clean
  after. Two implementers reported the same one-shot load flake. If it recurs, harden
  the test clients' connect with a bounded retry - do not loosen assertions.

- Stop 4 `rc-style` (`687b286`): 5 files conflicted; the two predicted
  out-of-marker jobs done (Toolbar shims deleted for work-area's setters;
  FrameMetrics re-applied inside applyMaximizedGeometry; plus a second Toolbar
  construction site in reconfigure moved to the Output-owning ctor). Gate 50/50,
  91%; one golden flipped by design.
- Stop 5 `menu-wire` (`94b2eec` + Task-9 `4d40079`): stubs died per their own
  comments; menuFile wired from Config at boot + reconfigure; hermetic pins in
  4 test files; the [reconfig] cache test adapted (mechanism, not requirement).
  Gate 51/51, 91%, goldens clean. WAVE 1 CODE-COMPLETE.
- Review-worthy items carried into the wave review: sni GetAll-error drops the
  registration (product question: decouple watcher bookkeeping from host
  materialization?); reconfigure() runs rc rootCommand with no headless gate;
  Server::loadMenuFile reads menu_file_[0] behind a guard a future caller could
  bypass; work-area's hot-unplug re-home semantics are new behavior.

- Review round (2026-07-04): 5-dimension review + per-finding adversarial verify
  over 643395e..HEAD -> 23 confirmed, 0 refuted (4 must-fix: toolbar re-home rc
  bypass, strut-vs-live-metrics, focus-on-map under lock, SNI 32-bit dim wrap).
  Fixed in 4 territory-disjoint worktrees (w1fix/{server,style,sni,tests}), all
  23 fixed, none deferred, merged conflict-free. Final gate 52/52, 91%, goldens
  clean. Pushed to the fork @ 8a3fc49. Full findings JSON in the session
  scratchpad wave1-review/ (ephemeral; everything actionable is in the commits).
- New watch item from the fix round: tests/system/retheme_test.cc:99
  CHECK(sourcePath().empty()) is install-prefix-sensitive (BBAI_DEFAULT_STYLE
  ships in data/styles) - same class as the menuFile pins; fix mechanism differs,
  owed to wave 2.
- Behavioral change downstream should know: headless Servers default to a
  recording FakeCommandRunner - no headless path can fork, structurally.

## Wave 2 (locked 2026-07-04)

User-owned, asked and answered:
- **Focus model + placement: implement both now.** Sloppy focus + AutoRaise/ClickRaise
  and Cascade/Center/RowSmart placement - in window-mgmt's territory (it owns
  View/behavior); configmenu ships the live UI + persist. Kills the fixed-(160,120)
  map wart.
- **Sloppy focus is DEFAULT-ON** (user: "focus-follows-mouse is something I like and
  want default-on") - when session.focusModel is absent, default SloppyFocus (no
  AutoRaise). An rc key always wins. General principle, program-wide: behavior choices
  like this are config-file configurable, never hardcoded.
- **Keys confirmed**: Super+F fullscreen, Super+Shift+Left/Right snap,
  Super+Ctrl+arrows move-to-output.
- **Slit icon_name fallback: decode theme icons** (libpng already linked in src/ -
  the "expensive" pricing was a scout error; cost is search-path policy only).
- **Parity gaps accepted**: no Shade (xdg-shell has no such concept); tray context
  menus render text-only (no icon column in bt::Menu).

Plan-author calls:
- Workspace count stays grow-only on the rc path (classic never shrank on
  reconfigure); shrink + re-home only via the explicit RemoveWorkspace action -
  configmenu owns the re-home debt Server.cc left it.
- onTop rows omitted from ALL three menus (toolbar/slit/window); the parsed keys stay
  inert; this line is the one documented deviation. No lying toggles, program law.
- Landing order: window-mgmt -> slit -> configmenu -> menus (rationale in the wave-2
  synthesis; menus performs the wave's ONE menu-golden re-bless if the style-margin
  swap moves pixels at all).
- Seam contracts pinned in the synthesis are LAW - notably
  `void Server::openSniContextMenu(const sni::Item &item, int lx, int ly)` (slit
  provides proxy body, menus swaps the body only) - the two scouts had pinned that
  same function under two different names; that clash dies here.
- Watch-item correction: the install-prefix pattern is at retheme_test.cc:138 (drifted
  from :99) AND style_boot_test.cc:78; configmenu owns fixing both with one mechanism.

## Wave-2 train log

Two-phase because menus consumes all three others' seams as real (not stubs):
- Phase A (parallel, off wave-1 tip): window-mgmt 13/13, slit 8/8, configmenu 8/8,
  each green + its sanctioned new goldens only.
- Stop 1 `window-mgmt`: clean merge, gate 63/63, 92%.
- Stop 2 `slit` (`e7e3c28`): 4 additive conflicts (Server.cc teardown, Toolbar
  containsGlobal/exposedHeight, tests/meson.build) - git-mediate; gate 64/64, 92%,
  goldens clean.
- Stop 3 `configmenu` (`4180861`): 2 additive conflicts (Server.hh setConfigOption
  decl, tests/meson.build) - git-mediate; gate 67/67, 92%, goldens clean.
- Phase B: menus branches off 4180861 (the integrated tip) so containsGlobal /
  Slit::currentRect / openSniContextMenu-proxy / Act::ConfigOption are all REAL.
- Stop 4 `menus`: clean merge (branched off the integrated tip); gate 71/71, 91%,
  goldens clean. menus caught + fixed TWO real bugs in its own plan's SniMenu
  design (async sd-bus needs a post-queue flush; proxy-fallback + client teardown
  must defer to a wl_event_loop idle so it never re-enters the Host's sd_bus_process
  dispatch - without it slit's landed ContextMenu test regressed). WAVE 2 CODE-COMPLETE.

- Review round (2026-07-04): 5-dimension review + per-finding adversarial verify over
  75488aa..HEAD -> 7 confirmed, 0 refuted (1 must-fix: demoted fullscreen covering the
  alt-tab target). Milder than wave-1's 23 - the plan-writer discipline held. All 6
  distinct fixes (findings [0]/[2] were one defect) landed in one worktree (concentrated
  in Server.cc), each with a pinning test watched failing first. Gate 71/71, 92%,
  goldens clean. WAVE 2 COMPLETE + pushed.

## Wave 3 (decisions, 2026-07-05)

Research found wlroots 0.20 ships a COMPLETE server-side ext_workspace_v1 helper -
the code slice is a thin bind (one extern-C include, a manager + one all-outputs
group, a syncExtWorkspaces() reconcile after each model mutation, ACTIVATE routed
through the existing setCurrentWorkspace choke point), NOT a hand-rolled protocol.

Plan-author calls:
- ext-workspace-v1: IN (user-locked twice: spec 3 + full-sweep). Scope ACTIVATE-only
  (export list/names/active + honor client activate; ignore create/remove/assign).
  Lands FIRST, gets its own adversarial review (only runtime code in wave 3). The
  teardown trap is real: ext_workspace_commit listener MUST disconnect before
  wl_display_destroy (manager asserts wl_list_empty), same spot as new_output.
- COPR owner = hila-shemer (default; the ONE string to change - lives in spec
  Source0/URL + the README `dnf copr enable` line). RPM targets fedora-44 ONLY
  (wlroots ABI breaks every minor; f44 is the CI chroot with 0.20.x).
- Ship a starter data/menu (small, mirrors the built-in) + its install target + RPM
  %files line, so the advertised /usr/share/blackboxai/menu exists and is editable.
- Man page: doc/blackboxai.1.in via a meson configure_file (define pkgdatadir/
  defaultmenu - none exist today). Prose docs under docs/ (install.md, gdm-session.md,
  screenshots/). RPM %doc/%files reference both trees.
- README screenshots: copy the 8-image story set into docs/screenshots/ (stable, no
  harness dependency) - the set already shown to the user.
- Fix-in-passing (doc writers must not paraphrase stale headers): Keybindings.hh:1-5
  says rc keybinding parsing is 'M5' - it never shipped; keybindings are NOT rc-
  configurable yet. Say so honestly.
- Land order: ext-workspace -> man page -> RPM (lockstep with man) -> README+install.
  Then the program-wide final review + demo/screenshot regen + push. No inter-slice
  code seam this wave (ext-workspace is standalone; docs only cross-reference).

## Parked defects (found by scouts, not wave-1 work)

- No cursor axis handler - scroll never reaches clients. Real daily-driver bug; parked to
  **wave-2 window-mgmt** explicitly so it doesn't fall through.
- `Menu::show` clamps to the primary output on multi-head - parked to **wave-2 menus**.

## Research artifacts

Scout + synthesis JSON (session scratchpad, ephemeral):
`/tmp/claude-1000/-home-hila-proj-blackboxai/8b5e93a4-d1a9-4374-9185-6a529349f965/scratchpad/wave1-research/`.
Everything load-bearing from them is baked into the slice plans; the scratchpad copies are
for this session's convenience only.
