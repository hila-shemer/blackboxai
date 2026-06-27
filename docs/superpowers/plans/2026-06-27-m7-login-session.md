# M7 Login Session (slice 1) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Commit messages: author with the **hila-voice** skill (personal repo) and end each body with the Co-Authored-By trailer below.

**Goal:** Make BlackboxAI a session you can log into on a TTY and stay in — keep the session across VT-switches, fail loud instead of black-screen, light up every monitor, autostart your usual agents, ship a `.desktop`, and a key to quit a wedged session.

**Architecture:** Small, surgical changes to `Server.cc` (session retention, loud-fail, multi-output, autostart call) plus one new pure module (`Autostart`) and one keybinding. The session/VT-switch path is real-hardware-only and is *code-complete-but-hand-verified* — everything else is unit-tested headless.

**Tech Stack:** C++20, wlroots 0.20 (`wlr_session`, `wlr_backend_autocreate`/`_start`), meson/ninja, doctest, the existing `bt::Resource`/`CommandRunner`.

## Global Constraints

- wlroots **0.20**; headless backend for all automated tests (`WLR_BACKENDS=headless WLR_RENDERER=pixman`).
- Build/test: `meson setup build -Db_coverage=true -Dbuildtype=debug && ninja -C build && meson test -C build --suite unit` and, for Server-touching tasks, `… meson test -C build --suite system` (needs `XDG_RUNTIME_DIR=$(mktemp -d); chmod 700 "$XDG_RUNTIME_DIR"`).
- Coverage gate ≥ 80% (`gcovr -r . build --filter 'toolkit/' --filter 'src/' --fail-under-line=80`); don't regress.
- One logical task per commit, each compile+test-green. hila-voice commit messages, spaced-hyphen ` - ` not em-dash.
- Co-Authored-By trailer (verbatim): `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`
- Do not move existing golden PNGs. Multi-output/toolbar changes must keep the existing single-output goldens byte-identical (the existing tests add exactly one headless output).
- Spec: `docs/superpowers/specs/2026-06-27-m7-login-session-design.md`.

---

### Task 1: XDG autostart parser (pure)

**Files:**
- Create: `src/Autostart.hh`, `src/Autostart.cc`
- Modify: `src/meson.build` (add `Autostart.cc` to `bbai_lib`)
- Test: `tests/unit/autostart_test.cc`; `tests/meson.build` (register, suite `unit`)
- Fixtures: `tests/fixtures/autostart/` (a few `.desktop` files)

**Interfaces:**
- Produces:
  - `struct bbai::DesktopEntry { std::string exec, try_exec; bool hidden = false; std::vector<std::string> only_show_in, not_show_in; };`
  - `DesktopEntry bbai::parseDesktopEntry(const std::string &contents);` — reads the `[Desktop Entry]` group only; ignores other groups; last value wins; unknown keys ignored.
  - `bool bbai::shouldAutostart(const DesktopEntry &e, const std::string &current_desktop);` — true unless `hidden`, or `only_show_in` non-empty and lacks `current_desktop`, or `not_show_in` contains `current_desktop`. (TryExec/PATH handled in Task 6 wiring, kept out of the pure layer.)
  - `std::string bbai::stripFieldCodes(const std::string &exec);` — removes the field codes `%f %F %u %U %i %c %k %d %D %n %N %v %m`, leaves a literal `%%` as `%`, collapses resulting double spaces, trims.

- [ ] **Step 1: Write the failing tests** in `tests/unit/autostart_test.cc`:

```cpp
#include <doctest/doctest.h>
#include "Autostart.hh"
using namespace bbai;

TEST_CASE("parseDesktopEntry pulls the [Desktop Entry] keys") {
  DesktopEntry e = parseDesktopEntry(
    "[Desktop Entry]\nType=Application\nExec=nm-applet --indicator\n"
    "OnlyShowIn=GNOME;Blackbox;\nTryExec=nm-applet\n"
    "[Other]\nExec=should-be-ignored\n");
  CHECK(e.exec == "nm-applet --indicator");
  CHECK(e.try_exec == "nm-applet");
  CHECK(e.only_show_in == std::vector<std::string>{"GNOME", "Blackbox"});
  CHECK_FALSE(e.hidden);
}

TEST_CASE("shouldAutostart honors Hidden / OnlyShowIn / NotShowIn") {
  DesktopEntry base; base.exec = "x";
  CHECK(shouldAutostart(base, "Blackbox"));                       // no constraints -> run
  DesktopEntry hidden = base; hidden.hidden = true;
  CHECK_FALSE(shouldAutostart(hidden, "Blackbox"));
  DesktopEntry only = base; only.only_show_in = {"GNOME"};
  CHECK_FALSE(shouldAutostart(only, "Blackbox"));                 // not listed -> skip
  only.only_show_in = {"GNOME", "Blackbox"};
  CHECK(shouldAutostart(only, "Blackbox"));                       // listed -> run
  DesktopEntry no = base; no.not_show_in = {"Blackbox"};
  CHECK_FALSE(shouldAutostart(no, "Blackbox"));
}

TEST_CASE("stripFieldCodes removes %-codes, keeps %%") {
  CHECK(stripFieldCodes("foo %U --bar %i") == "foo --bar");
  CHECK(stripFieldCodes("baz %%literal") == "baz %literal");
}
```

- [ ] **Step 2: Run, verify it fails to compile** (`Autostart.hh` missing):

Run: `meson test -C build --suite unit` — Expected: build error / FAIL.

- [ ] **Step 3: Implement `src/Autostart.hh` + `src/Autostart.cc`.** Parse line-by-line: track whether we're inside `[Desktop Entry]`; split `key=value` on the first `=`; for `OnlyShowIn`/`NotShowIn` split the `;`-terminated list dropping the trailing empty; `Hidden=true` (case-sensitive `true`) sets `hidden`. `stripFieldCodes`: scan for `%`, map `%%`→`%`, drop a known code char, else keep; then collapse runs of spaces and trim. (Pure; no filesystem here.)

- [ ] **Step 4: Run tests, verify pass.** Run: `meson test -C build --suite unit` — Expected: PASS.

- [ ] **Step 5: Commit.**

```bash
git add src/Autostart.hh src/Autostart.cc src/meson.build tests/unit/autostart_test.cc tests/meson.build
git commit  # hila-voice: "Autostart: parse + filter .desktop entries (pure)"
```

---

### Task 2: `blackboxai.desktop` + meson install + well-formedness test

**Files:**
- Create: `data/blackboxai.desktop`
- Modify: `meson.build` (top-level — `install_data('data/blackboxai.desktop', install_dir: get_option('datadir') / 'wayland-sessions')`)
- Test: `tests/unit/desktop_file_test.cc` (+ register, suite `unit`); pass the path via a `-DBBAI_SESSION_DESKTOP="…"` compile define like the menu/config fixtures do.

**Interfaces:** none (data + a static-content test).

- [ ] **Step 1: Write the failing test** — assert the shipped file has the required keys:

```cpp
#include <doctest/doctest.h>
#include <fstream>
#include <sstream>
#include <string>
TEST_CASE("blackboxai.desktop is a well-formed wayland session") {
  std::ifstream f(BBAI_SESSION_DESKTOP); REQUIRE(f.good());
  std::stringstream ss; ss << f.rdbuf(); std::string s = ss.str();
  CHECK(s.find("[Desktop Entry]") != std::string::npos);
  CHECK(s.find("\nExec=blackboxai") != std::string::npos);
  CHECK(s.find("\nType=Application") != std::string::npos);
  CHECK(s.find("\nName=BlackboxAI") != std::string::npos);
}
```

- [ ] **Step 2: Run, verify fail** (file missing / define unset). Run: `meson test -C build --suite unit`.

- [ ] **Step 3: Create `data/blackboxai.desktop`:**

```ini
[Desktop Entry]
Name=BlackboxAI
Comment=Blackbox, reborn on Wayland
Exec=blackboxai
Type=Application
DesktopNames=Blackbox
```

Wire `install_data` + the `-DBBAI_SESSION_DESKTOP` define.

- [ ] **Step 4: Run, verify pass.** `meson test -C build --suite unit`.

- [ ] **Step 5: Commit.** hila-voice: `"Session: ship a wayland-sessions .desktop so a DM lists us"`.

---

### Task 3: Fail loud on backend-start failure

**Files:** Modify `src/Server.hh:39` (the `ok()` body + a `bool started_` member), `src/Server.cc:181` (capture `wlr_backend_start`'s return + stderr error). Test: `tests/unit/server_ok_test.cc` (pure predicate).

**Interfaces:** Produces a free function `bool bbai::serverStarted(bool have_display, bool have_backend, bool backend_started);` (the `ok()` predicate, extracted so it's testable without faking a backend).

- [ ] **Step 1: Failing test** in `tests/unit/server_ok_test.cc`:

```cpp
#include <doctest/doctest.h>
#include "ServerOk.hh"
using namespace bbai;
TEST_CASE("ok() requires the backend to have started") {
  CHECK(serverStarted(true, true, true));
  CHECK_FALSE(serverStarted(true, true, false));   // created but start failed
  CHECK_FALSE(serverStarted(true, false, true));
  CHECK_FALSE(serverStarted(false, true, true));
}
```

- [ ] **Step 2: Run, verify fail.**
- [ ] **Step 3: Implement.** Add `src/ServerOk.hh` with `inline bool serverStarted(bool d, bool b, bool s){ return d && b && s; }`. In `Server.cc` capture `started_ = wlr_backend_start(backend);` and on `!started_` print to stderr: `"blackboxai: backend failed to start - no seat, or DRM master held by another session"`. Change `Server.hh` `ok()` to `return serverStarted(display != nullptr, backend != nullptr, started_);` and add `bool started_ = false;`. (`main.cc`'s existing `!ok()` branch already prints its generic line + returns 1.)
- [ ] **Step 4: Run unit + system suites, verify pass** (system suite proves the normal headless path still has `ok()==true`).
- [ ] **Step 5: Commit.** hila-voice: `"Server: ok() means the backend actually started, not just exists"`. *(Note: the real start-failure path is hand-verified in Task 8's sabotage check — headless always starts.)*

---

### Task 4: Escape hatch — `Ctrl+Alt+Backspace` quits

**Files:** Modify `src/Keybindings.hh` (add `Quit` to the `Action::Kind` enum), `src/Keybindings.cc` (add `CTRL` const + the binding), `src/Server.cc` (`executeAction` → `case Action::Quit: terminate(); break;`). Test: extend `tests/unit/keybinding_test.cc`.

**Interfaces:** Consumes `Action::Kind::Quit`; the binding `{CTRL|ALT, XKB_KEY_BackSpace, {Action::Quit}}`.

- [ ] **Step 1: Failing test** in `tests/unit/keybinding_test.cc` — assert the matcher maps Ctrl+Alt+Backspace to `Quit`:

```cpp
TEST_CASE("Ctrl+Alt+Backspace is the session-quit chord") {
  Keybindings kb;
  Action a = kb.dispatch(WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_BackSpace);
  CHECK(a.kind == Action::Quit);
}
```

- [ ] **Step 2: Run, verify fail** (`Action::Quit` undefined).
- [ ] **Step 3: Implement.** Add `Quit` to the enum (`Keybindings.hh:19` list). In `Keybindings.cc` add `const uint32_t CTRL = WLR_MODIFIER_CTRL;` and `{ CTRL | ALT, XKB_KEY_BackSpace, { Action::Quit } },`. In `Server.cc` `executeAction`, add `case Action::Quit: terminate(); break;`.
- [ ] **Step 4: Run unit suite, verify pass.** Optionally add a system test: `injectKeyForTest(XKB_KEY_BackSpace, WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, true)` then `CHECK(server.lastActionForTest() == Action::Quit)`.
- [ ] **Step 5: Commit.** hila-voice: `"Keybindings: Ctrl+Alt+Backspace quits - escape a wedged session"`.

---

### Task 5: Light up every output

**Files:** Modify `src/Server.cc:165-172` (`new_output` handler) + add a `std::vector<std::unique_ptr<Output>> outputs_;` member in `Server.hh` (keep `active_output` = the first head). Test: `tests/system/multi_output_test.cc`.

**Interfaces:** Consumes `Output(Server&, wlr_output*)` (self-registers in `output_layout`, per `Output.cc:27`).

- [ ] **Step 1: Failing test** in `tests/system/multi_output_test.cc` — add two headless heads, assert both become scene outputs and the toolbar is on the first:

```cpp
// headless: add a second output, both should light up
server.addHeadlessOutputForTest(1280, 720);   // add a test helper that calls wlr_headless_add_output
for (int i = 0; i < 50 && server.outputCountForTest() < 2; ++i) server.dispatch();
CHECK(server.outputCountForTest() == 2);
CHECK(server.toolbarForTest() != nullptr);     // toolbar still exists (on primary)
```

- [ ] **Step 2: Run, verify fail** (only one output today).
- [ ] **Step 3: Implement.** In `new_output`: always construct an `Output` for the head and `push_back` into `outputs_`; set `active_output` and create the `toolbar_` only on the first (`if (!active_output)`). The default-cursor call (added earlier) stays inside the first-head branch. Add `outputCountForTest()`/`addHeadlessOutputForTest()` test accessors. Leave maximize/work-area primary-bound (deferred).
- [ ] **Step 4: Run system suite, verify pass + existing single-output goldens unchanged.** Run with the headless env. Expected: all green, no golden movement.
- [ ] **Step 5: Commit.** hila-voice: `"Server: bring up every output, not just the first head"`.

---

### Task 6: Wire autostart at startup

**Files:** Modify `src/Server.cc` (after the socket is added / before `run()`, spawn the autostart set; export `XDG_CURRENT_DESKTOP=Blackbox`), reuse `Autostart` (Task 1) + `commandRunner()`. Add a `bool onPath(const std::string&)` helper for `TryExec`. Test: `tests/system/autostart_wiring_test.cc` using a `FakeCommandRunner` + a temp autostart dir.

**Interfaces:** Consumes `parseDesktopEntry`, `shouldAutostart`, `stripFieldCodes` (Task 1), `CommandRunner::run`.

- [ ] **Step 1: Failing test** — point the autostart dir at a temp fixture (via a test setter `setAutostartDirForTest(path)`), drop two `.desktop` files (one `Hidden=true`), install a `FakeCommandRunner`, run autostart, assert only the visible one was launched:

```cpp
server.setCommandRunnerForTest(&fake);
server.setAutostartDirsForTest({tmpdir});      // a vector<string> override
server.runAutostartForTest();
CHECK(fake.runCount() == 1);
CHECK(fake.lastCommand().back().find("good-app") != std::string::npos);
```

- [ ] **Step 2: Run, verify fail.**
- [ ] **Step 3: Implement.** `runAutostart()`: for each dir (user `~/.config/autostart` then `/etc/xdg/autostart`, or the test override), glob `*.desktop`, dedup by basename (first dir wins), `parseDesktopEntry`, `shouldAutostart(e, "Blackbox")`, skip if `try_exec` non-empty and `!onPath(try_exec)`, else `commandRunner().run({"/bin/sh","-c", stripFieldCodes(e.exec)})`. Call it from the ctor tail (non-headless only) and `setenv("XDG_CURRENT_DESKTOP","Blackbox",1)` before spawning. Expose `runAutostartForTest`/`setAutostartDirsForTest`.
- [ ] **Step 4: Run unit + system suites, verify pass.**
- [ ] **Step 5: Commit.** hila-voice: `"Server: run XDG autostart on login (skip Hidden/wrong-desktop)"`.

---

### Task 7: Retain the session, re-render on VT-switch *(hand-verified)*

**Files:** Modify `src/Server.cc:36` (`wlr_backend_autocreate(loop, &session_)`) + a `wlr_session *session_ = nullptr;` and `bt::Listener session_active;` in `Server.hh`. Include `<wlr/backend/session.h>` via `wlr.hpp` if not already.

**Interfaces:** wlroots 0.20 (verified from headers): `wlr_backend_autocreate(struct wl_event_loop *, struct wlr_session **)`; `struct wlr_session { bool active; struct { struct wl_signal active; } events; … }`.

- [ ] **Step 1: Implement (no headless test — see note).** Pass `&session_` to `wlr_backend_autocreate`. If `session_` is non-null (DRM path; null under nested/headless), connect `session_active` to `session_->events.active` with a handler: `if (session_ && session_->active) { for (auto &o : outputs_) o->scheduleFrame(); }` (add `Output::scheduleFrame()` → `wlr_output_schedule_frame` on the wrapped output, or damage the scene output). Guard everything on `session_ != nullptr` so the nested/headless paths are unaffected.
- [ ] **Step 2: Build + run the full suite** — confirm **no regression** (headless leaves `session_` null, so this is inert there). Run unit + system suites; expect all green, goldens unmoved.
- [ ] **Step 3: Commit.** hila-voice: `"Server: keep the session and re-render on VT-switch back"`. Body must state plainly that the active path is **hand-verified on a TTY (Task 8), not headless-testable** — headless has no session.

> **Note (why no automated test):** the session/VT path only exists on the DRM backend with a real seat. Faking `wlr_session` is exactly the heavy mock infra this project avoids (cf. the keyboard-device coverage decision). Verified in Task 8.

---

### Task 8: TTY hand-verification + finish

Not code — the manual gate for the parts CI can't reach. Run on the real machine, record pass/fail in the commit/notes.

- [ ] Build release-ish: `meson setup buildtty -Dbuildtype=debugoptimized && ninja -C buildtty`.
- [ ] `sudo ninja -C buildtty install` (or copy `blackboxai.desktop` to `/usr/share/wayland-sessions/` + the binary on `PATH`).
- [ ] **Ctrl+Alt+F3**, log in to the TTY, run `blackboxai`. Verify: gradient desktop + toolbar on the primary head; **every monitor lit**.
- [ ] Confirm autostart agents are up (e.g. a tray/polkit prompt where you'd expect one).
- [ ] **Ctrl+Alt+F1** to GNOME and back to BlackboxAI — screen **returns, not blank**.
- [ ] **Ctrl+Alt+Backspace** — clean exit back to the login prompt.
- [ ] Sabotage: with another session holding the seat, launch — expect the **loud stderr line**, not a black hang.
- [ ] Only after the above: reboot to GDM, pick **BlackboxAI**, log in.
- [ ] Record results; file any failures as follow-up tasks (do not paper over).

---

## Self-review notes
- Spec §3.1→Task 7, §3.2→Task 3, §3.3→Task 5, §3.4→Tasks 1+6, §3.5→Task 2, §3.6→Task 4, §4 automated→Tasks 1-6 tests, §4 hand-verify→Task 8. All covered.
- Hand-verify scope is fenced to Task 7's session path only; everything else is headless-tested.
