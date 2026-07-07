# Configurable Keybindings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a user replace the hardcoded keybinding table with a fluxbox-style keys file (`session.keyFile`, default `~/.blackboxai/keys`), and bind keys to `Exec <command>`.

**Architecture:** Add a pure line parser + file loader to the existing `bbai::Keybindings` (keeping its `dispatch()` normalisation contract), surface a `keyFile` path through `Config`, and have `Server` load it at boot and on `reconfigure()` into the same `keybindings_` member. `Exec` routes through the existing `CommandRunner` seam. A reserved `Ctrl+Alt+BackSpace → Quit` is always installed with precedence.

**Tech Stack:** C++20, wlroots 0.20, xkbcommon, meson/ninja, doctest. Design: `docs/superpowers/specs/2026-07-07-configurable-keybindings-design.md`.

## Global Constraints

- Build/test **only** in docker `blackboxai-ci:f44` — host has no wlroots.
  Invocation (shm must scale with parallelism; serialize the system suite):
  ```sh
  docker run --rm --shm-size=4g -v /home/hila/proj/blackboxai:/src -w /src blackboxai-ci:f44 bash -lc '<cmds>'
  ```
  System suite needs `export XDG_RUNTIME_DIR=$(mktemp -d); chmod 700 "$XDG_RUNTIME_DIR"; WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-kb --suite system --num-processes 1`.
- `rg` is not in the container — use `grep`.
- Coverage gate ≥80% lines on `toolkit/` + `src/` (project target ~90%); every task keeps the full suite green.
- Dispatch matches by modifier **equality** after `cleanMods()` (mask CapsLock+Mod2) and `xkb_keysym_to_lower`. Parsed bindings MUST be produced in that normalised shape.
- Unit test files define **no** `main` (smoke_test.cc carries the DOCTEST main); each system test is its own exe and defines `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`.
- Reuse the container build dir `build-kb` (already configured & green at plan time) so tasks are incremental.

---

## File structure

- `src/Keybindings.hh` / `src/Keybindings.cc` — the type: `Action` (grows `Exec` + `std::string exec`), public `Binding`, `builtinDefaults()`, `parseLine()`, `loadFile()`. Owns all parsing.
- `src/Config.hh` / `src/Config.cc` — new `std::string keyFile` field + `fromResource` read.
- `src/Server.cc` — ctor + `reconfigure()` load call; `executeAction()` `Exec` case.
- `data/keys` — shipped reference copy of the defaults.
- `meson.build` — install `data/keys`.
- `doc/blackboxai.1.in`, `README.md`, `src/Keybindings.hh` header comment — docs.
- `tests/unit/keybindings_file_test.cc` — parser + loader unit tests (no main).
- `tests/system/keybindings_file_test.cc` — end-to-end key-injection + Exec (own main).
- `tests/fixtures/keys-*.keys`, `tests/fixtures/keys-*.blackboxrc` — fixtures.

---

## PHASE 1 — Rebind existing actions

### Task 1: Extract `builtinDefaults()` and expose `Binding`

**Files:**
- Modify: `src/Keybindings.hh:38-41` (make `Binding` public, add `builtinDefaults`)
- Modify: `src/Keybindings.cc:5-36` (move table into the static)
- Test: `tests/unit/keybinding_test.cc` (existing — add a guard assert)

**Interfaces:**
- Produces: `struct Keybindings::Binding { uint32_t mods; xkb_keysym_t sym; Action action; };` (now public); `static std::vector<Binding> Keybindings::builtinDefaults();`

- [ ] **Step 1: Add a guard test** to `tests/unit/keybinding_test.cc` (new TEST_CASE) proving the defaults survive the refactor:
```cpp
TEST_CASE("builtinDefaults matches the constructed table") {
  bbai::Keybindings kb;
  const uint32_t SUPER = WLR_MODIFIER_LOGO;
  CHECK(kb.dispatch(SUPER, XKB_KEY_Right).kind == bbai::Action::WorkspaceNext);
  CHECK(kb.dispatch(SUPER, XKB_KEY_1).kind == bbai::Action::WorkspaceTo);
  CHECK(kb.dispatch(SUPER, XKB_KEY_1).arg == 0);
  CHECK(bbai::Keybindings::builtinDefaults().size() >= 20);
}
```

- [ ] **Step 2: Run — expect FAIL** (`builtinDefaults` undefined):
`docker … 'ninja -C build-kb && meson test -C build-kb --suite unit'` → compile error.

- [ ] **Step 3: Implement.** In `Keybindings.hh`, move `struct Binding {…}` above `private:` (public) and declare `static std::vector<Binding> builtinDefaults();`. In `Keybindings.cc`, rename the ctor-body table into the static and have the ctor delegate:
```cpp
std::vector<Keybindings::Binding> Keybindings::builtinDefaults() {
  const uint32_t SUPER = WLR_MODIFIER_LOGO;
  const uint32_t ALT   = WLR_MODIFIER_ALT;
  const uint32_t SHIFT = WLR_MODIFIER_SHIFT;
  const uint32_t CTRL  = WLR_MODIFIER_CTRL;
  return {
    { SUPER, XKB_KEY_Right, { Action::WorkspaceNext } },
    /* …the entire existing table, unchanged… */
    { SUPER | CTRL, XKB_KEY_Down, { Action::MoveToOutput, WLR_DIRECTION_DOWN } },
  };
}
Keybindings::Keybindings() { bindings_ = builtinDefaults(); }
```

- [ ] **Step 4: Run — expect PASS** (unit green).
- [ ] **Step 5: Commit:** `git commit -am "Keybindings: extract builtinDefaults(), expose Binding"`

---

### Task 2: `parseLine()` — the pure parser

**Files:**
- Modify: `src/Keybindings.hh` (declare `parseLine`; add `<optional>`, `<string>`)
- Modify: `src/Keybindings.cc` (implement + parse helpers)
- Create: `tests/unit/keybindings_file_test.cc`
- Modify: `tests/meson.build:5-49` (add file to `unit_sources`)

**Interfaces:**
- Produces: `static std::optional<Binding> Keybindings::parseLine(const std::string& line, std::string* err);` — returns a Binding for a valid line; `nullopt` with `*err` **empty** for blank/comment; `nullopt` with `*err` **set** for malformed.

- [ ] **Step 1: Write failing unit tests** in `tests/unit/keybindings_file_test.cc` (NO main):
```cpp
#include <doctest/doctest.h>
#include "Keybindings.hh"
using bbai::Keybindings; using bbai::Action;

static Action parse(const std::string& l) {
  std::string err; auto b = Keybindings::parseLine(l, &err);
  REQUIRE_MESSAGE(b.has_value(), err);
  return b->action;
}

TEST_CASE("modifiers: aliases and fluxbox tokens both resolve") {
  std::string err;
  CHECK(Keybindings::parseLine("Super Right :NextWorkspace", &err)->mods == WLR_MODIFIER_LOGO);
  CHECK(Keybindings::parseLine("Mod4 Right :NextWorkspace", &err)->mods == WLR_MODIFIER_LOGO);
  CHECK(Keybindings::parseLine("Alt Tab :NextWindow", &err)->mods == WLR_MODIFIER_ALT);
  CHECK(Keybindings::parseLine("Control Alt BackSpace :Quit", &err)->mods
        == (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT));
  CHECK(Keybindings::parseLine("None space :RootMenu", &err)->mods == 0);
}
TEST_CASE("keysym resolves case-insensitively") {
  std::string err;
  CHECK(Keybindings::parseLine("Super f :ToggleFullscreen", &err)->sym == XKB_KEY_f);
  CHECK(Keybindings::parseLine("Super F7 :Screenshot", &err)->sym == XKB_KEY_F7);
}
TEST_CASE("actions and args") {
  CHECK(parse("Super Right :NextWorkspace").kind == Action::WorkspaceNext);
  CHECK(parse("Super 2 :Workspace 2").kind == Action::WorkspaceTo);
  CHECK(parse("Super 2 :Workspace 2").arg == 1);              // 1-based file
  CHECK(parse("Super Control Left :MoveToOutput Left").arg == WLR_DIRECTION_LEFT);
  CHECK(parse("Super q :close").kind == Action::CloseWindow); // action case-insensitive
}
TEST_CASE("blank and comment lines are silently skipped") {
  std::string err;
  CHECK_FALSE(Keybindings::parseLine("   ", &err).has_value());  CHECK(err.empty());
  CHECK_FALSE(Keybindings::parseLine("# a comment", &err).has_value()); CHECK(err.empty());
}
TEST_CASE("malformed lines report an error") {
  std::string err;
  CHECK_FALSE(Keybindings::parseLine("Super :NextWorkspace", &err).has_value()); // no key
  CHECK_FALSE(err.empty());
  CHECK_FALSE(Keybindings::parseLine("Bogus q :NextWorkspace", &err).has_value()); // bad mod
  CHECK_FALSE(Keybindings::parseLine("Super q :Frobnicate", &err).has_value());   // bad action
  CHECK_FALSE(Keybindings::parseLine("Super q :Workspace", &err).has_value());    // missing arg
  CHECK_FALSE(Keybindings::parseLine("Super zzzz :Quit", &err).has_value());      // bad keysym
}
```
Register in `tests/meson.build` `unit_sources`: add `'unit/keybindings_file_test.cc',`.

- [ ] **Step 2: Run — expect FAIL** (`parseLine` undefined): unit compile error.

- [ ] **Step 3: Implement `parseLine`** in `Keybindings.cc` (add `#include <optional>`, `<sstream>`, `<algorithm>` in the .cc; `<optional>`,`<string>` in the .hh). Helpers + parser:
```cpp
namespace {
  std::string lower(std::string s){ for(char&c:s) c=(char)tolower((unsigned char)c); return s; }

  bool modToken(const std::string& t, uint32_t& out) {
    std::string k = lower(t);
    if (k=="mod4"||k=="super"||k=="mod")   { out|=WLR_MODIFIER_LOGO;  return true; }
    if (k=="mod1"||k=="alt")               { out|=WLR_MODIFIER_ALT;   return true; }
    if (k=="control"||k=="ctrl")           { out|=WLR_MODIFIER_CTRL;  return true; }
    if (k=="shift")                        { out|=WLR_MODIFIER_SHIFT; return true; }
    if (k=="mod5")                         { out|=WLR_MODIFIER_MOD5;  return true; }
    if (k=="none")                         { return true; } // no bit
    return false;
  }
  bool dirToken(const std::string& t, int& out) {
    std::string k = lower(t);
    if (k=="left")  { out=WLR_DIRECTION_LEFT;  return true; }
    if (k=="right") { out=WLR_DIRECTION_RIGHT; return true; }
    if (k=="up")    { out=WLR_DIRECTION_UP;    return true; }
    if (k=="down")  { out=WLR_DIRECTION_DOWN;  return true; }
    return false;
  }
}

std::optional<Keybindings::Binding>
Keybindings::parseLine(const std::string& line, std::string* err) {
  auto fail = [&](const char* m){ if(err)*err=m; return std::optional<Binding>{}; };
  if (err) err->clear();
  // trim leading ws; blank / comment => silent skip
  size_t s = line.find_first_not_of(" \t");
  if (s == std::string::npos || line[s]=='#') return {};
  size_t colon = line.find(':', s);
  if (colon == std::string::npos) return fail("no ':action'");

  // LEFT: modifiers + key
  std::istringstream ls(line.substr(s, colon - s));
  std::vector<std::string> left; for (std::string t; ls>>t;) left.push_back(t);
  if (left.empty()) return fail("no key before ':'");
  uint32_t mods = 0;
  for (size_t i=0;i+1<left.size();++i) if(!modToken(left[i],mods)) return fail("unknown modifier");
  xkb_keysym_t sym = xkb_keysym_from_name(left.back().c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
  if (sym == XKB_KEY_NoSymbol) return fail("unknown key");

  // RIGHT: action + args ; command captured verbatim for :Exec (phase 2)
  std::istringstream rs(line.substr(colon+1));
  std::string name; rs >> name; std::string a = lower(name);
  Action act{};
  if      (a=="nextworkspace")     act.kind = Action::WorkspaceNext;
  else if (a=="prevworkspace")     act.kind = Action::WorkspacePrev;
  else if (a=="rootmenu")          act.kind = Action::OpenMenu;
  else if (a=="iconmenu")          act.kind = Action::IconMenu;
  else if (a=="close")             act.kind = Action::CloseWindow;
  else if (a=="nextwindow")        act.kind = Action::CycleNext;
  else if (a=="prevwindow")        act.kind = Action::CyclePrev;
  else if (a=="togglefullscreen")  act.kind = Action::ToggleFullscreen;
  else if (a=="snapleft")          act.kind = Action::SnapLeft;
  else if (a=="snapright")         act.kind = Action::SnapRight;
  else if (a=="screenshot")        act.kind = Action::Screenshot;
  else if (a=="quit")              act.kind = Action::Quit;
  else if (a=="workspace")     { int n; if(!(rs>>n)||n<1) return fail("Workspace needs N>=1");
                                 act.kind=Action::WorkspaceTo; act.arg=n-1; }
  else if (a=="movetooutput")  { std::string d; int dir; if(!(rs>>d)||!dirToken(d,dir))
                                 return fail("MoveToOutput needs a direction");
                                 act.kind=Action::MoveToOutput; act.arg=dir; }
  else return fail("unknown action");
  return Binding{ mods, sym, act };
}
```

- [ ] **Step 4: Run — expect PASS** (`meson test -C build-kb --suite unit`).
- [ ] **Step 5: Commit:** `git commit -am "Keybindings: parseLine() fluxbox-style parser + unit tests"`

---

### Task 3: `loadFile()` — authoritative table + reserved key

**Files:**
- Modify: `src/Keybindings.hh` / `.cc` (declare + implement `loadFile`)
- Modify: `tests/unit/keybindings_file_test.cc` (loader tests)

**Interfaces:**
- Produces: `bool Keybindings::loadFile(const std::string& path);` — replaces the table with the file's bindings (malformed lines skipped + diagnosed to stderr), prepends reserved `Ctrl+Alt+BackSpace → Quit`; returns `false` and leaves the current table untouched if the file cannot be opened.

- [ ] **Step 1: Write failing loader tests** (append to `keybindings_file_test.cc`). Uses a tmp file:
```cpp
#include <fstream>
#include <cstdio>
static std::string writeTmp(const std::string& body) {
  std::string p = std::string(std::tmpnam(nullptr)) + ".keys";
  std::ofstream(p) << body; return p;
}
TEST_CASE("loadFile replaces the table (authoritative)") {
  Keybindings kb;
  auto p = writeTmp("Super n :NextWorkspace\n# c\nSuper p :PrevWorkspace\n");
  REQUIRE(kb.loadFile(p));
  CHECK(kb.dispatch(WLR_MODIFIER_LOGO, XKB_KEY_n).kind == Action::WorkspaceNext);
  CHECK(kb.dispatch(WLR_MODIFIER_LOGO, XKB_KEY_p).kind == Action::WorkspacePrev);
  // a default that the file didn't set no longer fires:
  CHECK(kb.dispatch(WLR_MODIFIER_LOGO, XKB_KEY_Right).kind == Action::None);
  std::remove(p.c_str());
}
TEST_CASE("reserved Ctrl+Alt+BackSpace wins even if the file rebinds it") {
  Keybindings kb;
  auto p = writeTmp("Control Alt BackSpace :NextWorkspace\n");
  REQUIRE(kb.loadFile(p));
  CHECK(kb.dispatch(WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_BackSpace).kind == Action::Quit);
  std::remove(p.c_str());
}
TEST_CASE("malformed lines are skipped, valid ones kept") {
  Keybindings kb;
  auto p = writeTmp("Super n :NextWorkspace\nGarbage line\nSuper p :PrevWorkspace\n");
  REQUIRE(kb.loadFile(p));
  CHECK(kb.dispatch(WLR_MODIFIER_LOGO, XKB_KEY_n).kind == Action::WorkspaceNext);
  CHECK(kb.dispatch(WLR_MODIFIER_LOGO, XKB_KEY_p).kind == Action::WorkspacePrev);
  std::remove(p.c_str());
}
TEST_CASE("unreadable file returns false and keeps defaults") {
  Keybindings kb;
  CHECK_FALSE(kb.loadFile("/no/such/keys/file"));
  CHECK(kb.dispatch(WLR_MODIFIER_LOGO, XKB_KEY_Right).kind == Action::WorkspaceNext);
}
```

- [ ] **Step 2: Run — expect FAIL** (`loadFile` undefined).

- [ ] **Step 3: Implement `loadFile`** in `Keybindings.cc` (add `#include <fstream>`, `<iostream>`):
```cpp
bool Keybindings::loadFile(const std::string& path) {
  std::ifstream in(path);
  if (!in) return false;
  std::vector<Binding> next;
  // reserved escape valve, matched first (dispatch returns the first hit):
  next.push_back({ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_BackSpace, { Action::Quit } });
  std::string line; int n = 0;
  while (std::getline(in, line)) {
    ++n; std::string err;
    if (auto b = parseLine(line, &err)) next.push_back(*b);
    else if (!err.empty())
      std::cerr << "keys: " << path << ":" << n << ": " << err << " (skipped)\n";
  }
  bindings_ = std::move(next);
  return true;
}
```

- [ ] **Step 4: Run — expect PASS.**
- [ ] **Step 5: Commit:** `git commit -am "Keybindings: loadFile() authoritative table + reserved escape key"`

---

### Task 4: `Config::keyFile`

**Files:**
- Modify: `src/Config.hh:60`, `src/Config.cc:83`
- Test: `tests/unit/` config test (find the existing styleFile/menuFile case; add parallel keyFile assert)

**Interfaces:**
- Produces: `std::string Config::keyFile;` populated from `session.keyFile` (class `Session.KeyFile`), tilde-expanded, default `~/.blackboxai/keys`.

- [ ] **Step 1: Write failing test.** Locate the unit test that constructs a `Config` from a resource with `session.styleFile` (grep `styleFile` under `tests/`). Add asserts mirroring it:
```cpp
CHECK(cfg.keyFile == /* expanded ~/.blackboxai/keys when unset, or the set path */ );
```
(Follow the exact fixture style already used for styleFile/menuFile in that file.)

- [ ] **Step 2: Run — expect FAIL** (field missing → compile error).

- [ ] **Step 3: Implement.** `Config.hh` after `menuFile` (line 60):
```cpp
    std::string keyFile;     // session.keyFile;  default ~/.blackboxai/keys
```
`Config.cc` after the `menuFile` read (line 83):
```cpp
    cfg.keyFile = bt::expandTilde(res.read("session.keyFile", "Session.KeyFile",
                                           "~/.blackboxai/keys"));
```

- [ ] **Step 4: Run — expect PASS.**
- [ ] **Step 5: Commit:** `git commit -am "Config: session.keyFile path (default ~/.blackboxai/keys)"`

---

### Task 5: Server wire-in + live reload + system test

**Files:**
- Modify: `src/Server.cc` (ctor after `:94`; `reconfigure()` `:415-430`)
- Create: `tests/system/keybindings_file_test.cc`, `tests/fixtures/keys-rebind.keys`, `tests/fixtures/keys-rebind.blackboxrc`
- Modify: `tests/meson.build:197` region (new exe + test block)

**Interfaces:**
- Consumes: `Config::keyFile`, `Keybindings::loadFile`, `Server::injectKeyForTest`, `Server::lastActionForTest`, `Server::currentWorkspaceForTest`, `Server(bool headless, std::string rc_path)`.

- [ ] **Step 1: Write failing system test** `tests/system/keybindings_file_test.cc`:
```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <cstdlib>
#include "Server.hh"
#include "Keybindings.hh"
using namespace bbai;
TEST_CASE("a keys file rebinds a chord; the built-in default no longer fires") {
  setenv("WLR_BACKENDS","headless",1); setenv("WLR_RENDERER","pixman",1);
  Server server(true, "tests/fixtures/keys-rebind.blackboxrc");
  REQUIRE(server.ok());
  server.injectKeyForTest(XKB_KEY_n, WLR_MODIFIER_LOGO, true);   // Super+n :NextWorkspace
  CHECK(server.lastActionForTest() == Action::WorkspaceNext);
  CHECK(server.currentWorkspaceForTest() == 1);
  server.injectKeyForTest(XKB_KEY_Right, WLR_MODIFIER_LOGO, true); // default gone
  CHECK(server.currentWorkspaceForTest() == 1);
}
```
Fixtures: `tests/fixtures/keys-rebind.keys` = `Super n :NextWorkspace\n`; `tests/fixtures/keys-rebind.blackboxrc` = `session.keyFile: tests/fixtures/keys-rebind.keys\n` (relative resolves: system tests run at source root). Register in `tests/meson.build` copying the `keybinding_exe` block (`:197`):
```meson
kbfile_exe = executable('keybindings-file-test', files('system/keybindings_file_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('keybindings_file', kbfile_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)
```

- [ ] **Step 2: Run — expect FAIL** (Server ignores keyFile → default `n` does nothing, `Right` advances → workspace assert fails):
`… meson test -C build-kb keybindings_file --suite system --num-processes 1 -v`

- [ ] **Step 3: Implement.** In the Server ctor, immediately after `config_ = Config::load(rc_path_);` (`Server.cc:94`):
```cpp
    if (!config_.keyFile.empty())
      keybindings_.loadFile(config_.keyFile);   // false -> keep built-in defaults
```
In `reconfigure()` (alongside the config/menu/style reload, `Server.cc:415-430`), after `config_` is reloaded:
```cpp
    keybindings_ = Keybindings{};               // reset to built-in defaults
    if (!config_.keyFile.empty())
      keybindings_.loadFile(config_.keyFile);   // then re-apply the file if present
```

- [ ] **Step 4: Run — expect PASS**, then full suite green:
`… meson test -C build-kb --suite unit && WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-kb --suite system --num-processes 1`

- [ ] **Step 5: Commit:** `git commit -am "Server: load session.keyFile at boot + reconfigure; system test"`

---

### Task 6: Ship `data/keys`, install rule, docs

**Files:**
- Create: `data/keys`
- Modify: `meson.build:99`, `doc/blackboxai.1.in`, `README.md:46-50,120`, `src/Keybindings.hh:4-5`

- [ ] **Step 1: Create `data/keys`** = the defaults in friendly-alias form (header comment + every default binding), e.g.:
```
# BlackboxAI default keybindings. Copy to ~/.blackboxai/keys and edit.
# Grammar: <modifiers> <key> :<Action> [args].  Ctrl+Alt+BackSpace is reserved.
Super Right      :NextWorkspace
Super Left       :PrevWorkspace
Super 1          :Workspace 1
Super 2          :Workspace 2
Super 3          :Workspace 3
Super 4          :Workspace 4
Super space      :RootMenu
Super q          :Close
Alt Tab          :NextWindow
Alt Shift Tab    :PrevWindow
Super Tab        :NextWindow
Super Shift Tab  :PrevWindow
Super Alt t      :IconMenu
Super F7         :Screenshot
Super f          :ToggleFullscreen
Super Shift Left  :SnapLeft
Super Shift Right :SnapRight
Super Control Left  :MoveToOutput Left
Super Control Right :MoveToOutput Right
Super Control Up    :MoveToOutput Up
Super Control Down  :MoveToOutput Down
```

- [ ] **Step 2: meson install** — `meson.build` after `data/menu` (`:99`):
```meson
install_data('data/keys', install_dir : get_option('datadir') / 'blackboxai')
```

- [ ] **Step 3: Docs.** `README.md`: rewrite the `## Keybindings` section (`46-50`) to state bindings are configurable via `session.keyFile` (default `~/.blackboxai/keys`), keep the default table, note the reserved escape key; delete the limitations bullet (`120`). `doc/blackboxai.1.in`: add a KEYS section (grammar, action list incl. `Exec`, reserved key, `session.keyFile`). `src/Keybindings.hh:4-5`: delete the "M5 … hardcoded" sentence.

- [ ] **Step 4: Verify** the tree still configures/builds and man page generates:
`… 'ninja -C build-kb'` (install rule parse) and confirm no test regressions.

- [ ] **Step 5: Commit:** `git commit -am "keybindings: ship data/keys, install rule, man page + README"`

---

## PHASE 2 — `Exec`

### Task 7: `Action::Exec` + payload + parser

**Files:**
- Modify: `src/Keybindings.hh:16-25` (`Exec` kind + `std::string exec`)
- Modify: `src/Keybindings.cc` (`parseLine` `:Exec` branch)
- Modify: `tests/unit/keybindings_file_test.cc`

**Interfaces:**
- Produces: `Action::Exec`; `std::string Action::exec` (rest-of-line after `:Exec`, verbatim).

- [ ] **Step 1: Write failing unit tests:**
```cpp
TEST_CASE("Exec captures the command verbatim") {
  std::string err;
  auto b = Keybindings::parseLine("Super e :Exec thunar --new-window", &err);
  REQUIRE(b.has_value());
  CHECK(b->action.kind == Action::Exec);
  CHECK(b->action.exec == "thunar --new-window");
}
TEST_CASE("Exec with no command is malformed") {
  std::string err;
  CHECK_FALSE(Keybindings::parseLine("Super e :Exec", &err).has_value());
  CHECK_FALSE(err.empty());
}
```

- [ ] **Step 2: Run — expect FAIL** (`Exec` not a Kind).

- [ ] **Step 3: Implement.** `Keybindings.hh`: add `Exec` to the `Kind` enum and `std::string exec;` to `Action`. `parseLine`: add before the final `else return fail(...)`:
```cpp
  else if (a=="exec") {
    size_t p = line.find(':', s);
    std::string rest = line.substr(p+1);            // "Exec <cmd...>"
    size_t sp = rest.find_first_of(" \t");
    std::string cmd = (sp==std::string::npos) ? "" : rest.substr(sp+1);
    size_t b0 = cmd.find_first_not_of(" \t");
    if (b0==std::string::npos) return fail("Exec needs a command");
    act.kind = Action::Exec; act.exec = cmd.substr(b0);
  }
```

- [ ] **Step 4: Run — expect PASS.**
- [ ] **Step 5: Commit:** `git commit -am "Keybindings: :Exec action + verbatim command payload"`

---

### Task 8: `executeAction` Exec case + end-to-end test

**Files:**
- Modify: `src/Server.cc:1669-1694` (`executeAction` switch)
- Modify: `tests/system/keybindings_file_test.cc`; add `tests/fixtures/keys-exec.keys` + `.blackboxrc`
- Modify: `data/keys` (add an `Exec` example, commented), `doc/blackboxai.1.in` example

**Interfaces:**
- Consumes: `Server::commandRunner()`, `Server::setCommandRunnerForTest`, `FakeCommandRunner::runCount()/lastCommand()`.

- [ ] **Step 1: Write failing system test** (append):
```cpp
#include "CommandRunner.hh"
TEST_CASE("an :Exec binding routes through the command runner") {
  setenv("WLR_BACKENDS","headless",1); setenv("WLR_RENDERER","pixman",1);
  Server server(true, "tests/fixtures/keys-exec.blackboxrc");
  REQUIRE(server.ok());
  FakeCommandRunner fake; server.setCommandRunnerForTest(&fake);
  server.injectKeyForTest(XKB_KEY_e, WLR_MODIFIER_LOGO, true);   // Super+e :Exec kitty
  CHECK(fake.runCount() == 1);
  CHECK(fake.lastCommand() == std::vector<std::string>{"/bin/sh","-c","kitty"});
}
```
Fixtures: `keys-exec.keys` = `Super e :Exec kitty\n`; `keys-exec.blackboxrc` = `session.keyFile: tests/fixtures/keys-exec.keys\n`.

- [ ] **Step 2: Run — expect FAIL** (`Exec` hits `default:` / no spawn; `runCount()==0`).

- [ ] **Step 3: Implement.** In `executeAction` add:
```cpp
    case Action::Exec:
      commandRunner().run({"/bin/sh", "-c", a.exec});
      break;
```

- [ ] **Step 4: Run — expect PASS**, then full suite + coverage:
`… meson test -C build-kb --suite unit && WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-kb --suite system --num-processes 1 && gcovr -r . build-kb --filter toolkit/ --filter src/ --fail-under-line=80`

- [ ] **Step 5: Commit:** `git commit -am "Server: :Exec keybinding spawns via commandRunner; e2e test + docs"`

---

## Self-review (author)

- **Spec coverage:** grammar (T2), action vocab incl Exec (T2/T7), resolution order + default path (T4), authoritative + reserved key (T3), Exec via commandRunner (T8), live reload (T5), data/keys + install (T6), docs incl M5-comment removal (T6). All §s covered.
- **Type consistency:** `parseLine(const std::string&, std::string*)`, `loadFile(const std::string&)`, `builtinDefaults()`, `Binding{mods,sym,action}`, `Action{kind,arg,exec}` used identically across tasks.
- **Reserved-key precedence** relies on `dispatch()` returning the first match and `loadFile` pushing the reserved binding first — asserted in T3.
- **Note:** T4 Step 1 references the existing Config unit test by pattern (grep `styleFile`), not a fixed path — locate before writing.
