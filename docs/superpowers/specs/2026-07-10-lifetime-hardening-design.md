# Lifetime/UAF hardening — design

Date: 2026-07-10. Status: approved (user, this date).
Trigger: first dogfooding crash (core 24987, SIGSEGV View.cc:27) — a client destroyed
its xdg_toplevel role, kept the wl_surface, committed it again; the View's commit
listener dereferenced the freed toplevel. Fixed on `dogfood-fixes-crash-paste-bg`.
This effort hunts the rest of that class before it takes the session down again.

## Threat model

One bug class: a wlroots-owned object dies at client-controlled timing while
compositor state still references it. Four shapes, all with prior instances here:

- **(a) Listener outlives referent.** A `bt::Listener` lambda dereferences an object
  that can die before the listener disconnects. The crash above; also the historical
  request_* disconnect dance.
- **(b) Cached raw pointer not scrubbed.** `grabbed_view`, `focused_view`,
  `autoraise_pending_`, `pressed_button_view_`, `cycle_ring_`, menu targets,
  `active_output`… — each scrubbed by hand in removeView/onOutputDestroyed. Every
  new cache is a new obligation someone can forget.
- **(c) Teardown-order asserts.** wlroots asserts listener lists empty at destroy
  (seat request lists, ext-workspace commit list, backend new_output). Aborts the
  session as surely as a segfault.
- **(d) Role-vs-container lifetime mismatch.** xdg_toplevel vs wl_surface (the
  crash), zxdg_toplevel_decoration_v1 vs toplevel, lock surface vs output, SNI item
  vs bus name.

## Phases (infra-first, deliberately)

Verification precedes findings: adversarial reviews over-call (gotchas #22/#24 —
two of six M4 review findings were phantoms), and a finding with a running repro
under ASAN is a different currency than a finding with an argument.

### Phase 0 — Sanitizer floor
`-Db_sanitize=address,undefined` build config + a CI job running the full headless
suite under it. Leak checking OFF for now (`detect_leaks=0`): UAF is the target,
wlroots-global leaks are noise. Anything ASAN flags in the existing suite is fixed
or explicitly triaged before Phase 2 starts. Local invocation documented.

### Phase 1 — Hostile-client harness
TestClient grows an order-scriptable teardown driver:

- Destruction orders over {decoration, toplevel, xdg_surface, wl_surface, buffer}
  × lifecycle stages {pre-first-commit, mapped, mid-move grab, mid-resize grab,
  iconified, fullscreen, other-workspace, menu-open, mid-screenshot-select}.
- Post-teardown pokes: commit-after-role-destroy, stale/zero serials on selection
  and grabs, destroy-while-focused, destroy-mid-alt-tab-cycle.
- Contract: protocol-legal orders must not crash the server; illegal orders must
  produce a posted protocol error, not a crash.

Deliverable: a permanent `tests/system` hostile suite, green under ASAN.

### Phase 2 — Ultracode audit (the multi-agent phase)
- **Inventory (parallel readers, one per subsystem):** Server-input/grabs,
  focus/workspace/MRU, View/Decoration, Menu/IconMenu/WindowMenu, Toolbar/Slit,
  SessionLock, SNI/dbusmenu, Screenshot/Clipboard, Output/layout. Output per
  reader, structured: every `connect()` → (signal owner, objects the lambda
  dereferences, disconnect guarantee + where); every cached raw pointer →
  (scrub sites, can the referent die while cached?).
- **Merge/dedupe** in plain code (cross-subsystem matrix).
- **Adversarial verify per finding:** one agent refutes, one writes a
  hostile-client repro. CONFIRMED = running repro (crash or ASAN red) or an
  unrefuted hole with proven reachability. Unproven findings die.

Output: ranked confirmed findings + their repro tests.

### Phase 3 — Fixes
TDD, one commit per confirmed finding; the repro joins the hostile suite
permanently. Adversarial review of the completed batch (house pattern).

### Phase 4 — Crash forensics
- RPM ships usable symbols deliberately (debuginfo subpackage — today's journal
  backtrace resolved only because the binary happens to be unstripped).
- Async-signal-safe fatal-signal breadcrumb: one journald line, then re-raise so
  the core still drops.
- `wlr_log` routed to the journal under a session manager.

## Non-goals

Style/menu-file parsing robustness, OOM handling, GPU-loss recovery,
watchdog/respawn, and the `bt::Listener` auto-disconnect migration. The last is
revisited only if Phase 2 shows manual disconnects dominate confirmed findings —
then it's a data-backed migration, not a speculative one.

## Success criteria

- Full + hostile suites green under ASAN/UBSAN in CI.
- Every confirmed finding fixed with its repro in-tree.
- Zero known reachable lifetime holes at close.
- The next dogfood crash resolves to file:line from the journal alone.

## Known pre-existing issues (out of scope, tracked)

`session_lock` + `lock_interactions` fail on main (locker-crash-takeover flow) —
predates this effort, separate triage.
