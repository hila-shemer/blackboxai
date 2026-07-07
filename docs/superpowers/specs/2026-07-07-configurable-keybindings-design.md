# BlackboxAI — rc-configurable keybindings — Design

**Status:** approved (design, 2026-07-07)
**Date:** 2026-07-07
**Branch:** `wayland-rewrite`

## 1. What this is

Kill the "keybindings are fixed" limitation. Today the `(mods, keysym) → Action`
table is hardcoded in `Keybindings::Keybindings()` and never rebuilt. This slice
lets a user supply a **dedicated fluxbox-style keys file** that replaces the
built-in table, and adds a new **`Exec`** action so keys can launch programs.

Classic Blackbox had *no* built-in keybindings — they came from the external
`bbkeys` X11 app (`~/.bbkeysrc`), which has no Wayland equivalent. So there is no
legacy file to honour byte-for-byte; the grammar is ours to choose, and we choose
the fluxbox lineage because that is the syntax this user base actually knows.

## 2. Locked decisions (user, 2026-07-07)

- **Scope: both** — rebind the existing actions *and* bind keys to `Exec <command>`.
- **Grammar: a dedicated fluxbox-style keys file**, not inline rc resources.
- **`session.keyFile`** points at it; default resolution `~/.blackboxai/keys`.
- **Authoritative file** — when a keys file loads, it *is* the binding set
  (fluxbox behaviour), with **one reserved escape key**: `Ctrl+Alt+BackSpace →
  Quit` is always installed with precedence and cannot be shadowed or removed.

## 3. Grammar

One binding per line: `<modifiers...> <key> :<Action> [args]`. Blank lines and
`#` comments are skipped. A malformed line is skipped with a stderr diagnostic and
never aborts the parse (same philosophy as skipped pipe-menus).

```
# ~/.blackboxai/keys
Super Right              :NextWorkspace
Super 1                  :Workspace 1
Super q                  :Close
Super space              :RootMenu
Alt Tab                  :NextWindow
Alt Shift Tab            :PrevWindow
Super f                  :ToggleFullscreen
Super Shift Left         :SnapLeft
Super Control Left       :MoveToOutput Left
Super F7                 :Screenshot
Super e                  :Exec thunar
```

- **Modifiers** (case-insensitive, space-separated): fluxbox tokens `Mod1`
  `Mod4` `Control` `Shift` `Mod5`, plus friendly aliases `Alt`(=Mod1)
  `Super`/`Mod`(=Mod4) `Ctrl`(=Control). Unknown token → line skipped with
  diagnostic. `None` means no modifier.
- **Key**: any XKB keysym name, resolved via `xkb_keysym_from_name(...,
  XKB_KEYSYM_CASE_INSENSITIVE)` — so `Right`, `q`, `F7`, `space`, `BackSpace`,
  `ISO_Left_Tab` all resolve without a hand-maintained table. `XKB_KEY_NoSymbol`
  → line skipped.
- **Action**: colon-prefixed, case-insensitive, with args where the action needs
  one. The produced `(mods, keysym)` is normalised the same way `dispatch()`
  matches — `cleanMods()` (mask CapsLock+Mod2) + `xkb_keysym_to_lower` — so parsed
  bindings match by the existing modifier-**equality** contract.

## 4. Action vocabulary

String → existing `Action::Kind` (a 1:1 cover of today's 14 kinds) plus one new
kind:

| Token | Kind | arg / payload |
|---|---|---|
| `:NextWorkspace` | `WorkspaceNext` | — |
| `:PrevWorkspace` | `WorkspacePrev` | — |
| `:Workspace N` | `WorkspaceTo` | `arg = N-1` (file is 1-based) |
| `:RootMenu` | `OpenMenu` | — |
| `:IconMenu` | `IconMenu` | — |
| `:Close` | `CloseWindow` | — |
| `:NextWindow` | `CycleNext` | — |
| `:PrevWindow` | `CyclePrev` | — |
| `:ToggleFullscreen` | `ToggleFullscreen` | — |
| `:SnapLeft` / `:SnapRight` | `SnapLeft` / `SnapRight` | — |
| `:MoveToOutput <Left\|Right\|Up\|Down>` | `MoveToOutput` | `arg = WLR_DIRECTION_*` |
| `:Screenshot` | `Screenshot` | — |
| `:Quit` | `Quit` | — |
| `:Exec <command>` | **`Exec`** (new) | `exec = rest-of-line, verbatim` |

Unknown action token, missing required arg (`:Workspace` with no number,
`:MoveToOutput` with no/unknown direction) → line skipped with diagnostic.

## 5. Semantics

- **Resolution order:** (1) `session.keyFile` if set in the rc → (2) default
  `~/.blackboxai/keys` if that file exists → (3) compiled built-in defaults
  (byte-identical to today, so an existing user with no keys file sees **zero**
  behavioural change).
- **Authoritative:** a keys file that loads *replaces* the whole table.
- **Reserved key:** `Ctrl+Alt+BackSpace → Quit` is installed at the **front** of
  the table (matched first), so no keys file can shadow or drop the escape valve.
  Documented as reserved.
- **`Exec`:** dispatched to `Server::commandRunner().run({"/bin/sh","-c", exec})`
  — the same shell-routed seam as rc `rootCommand` (`Server.cc:370`) and XDG
  autostart (`Server.cc:810`). Shell exposure is acceptable here for the same
  reason `rootCommand` gets it: this is a user-authored file, not a downloaded
  theme. Under headless the default `FakeCommandRunner` records the argv, so it is
  testable and structurally cannot fork.
- **Live reload:** `reconfigure()` re-reads the keys file and rebuilds the table
  alongside the existing config/menu/style reload. A file that has since
  disappeared reverts to built-in defaults.

## 6. Code changes (by file, with anchors)

### Type (`src/Keybindings.hh` / `.cc`)
- Extend `struct Action` (`Keybindings.hh:16`): add `Exec` to `Kind`; add
  `std::string exec;` payload (keep `int arg`). `Action` is passed by value in
  the table; a `std::string` member is fine.
- Make `struct Binding` public (or add a public factory) so the parser and unit
  tests can build bindings.
- Add, keeping `dispatch()` and its normalisation contract unchanged:
  - `static std::vector<Binding> builtinDefaults();` — today's table (moved out of
    the ctor body, `Keybindings.cc:5-36`); the default ctor uses it.
  - `static std::optional<Binding> parseLine(const std::string& line,
    std::string* err);` — pure, unit-testable, no I/O. Handles modifiers, keysym,
    action + args, `Exec` remainder capture, comment/blank → `nullopt`.
  - `bool loadFile(const std::string& path);` — reads the file; on success
    replaces `bindings_` with the parsed set (skipping malformed lines, each with
    a stderr diagnostic) and **prepends** the reserved `Ctrl+Alt+BackSpace →
    Quit`. Returns `false` (leaving the current table intact) when the file cannot
    be opened, so the caller falls back to defaults.

### Config (`src/Config.hh` / `.cc`)
- `Config.hh` (session-file-paths block, after `menuFile`): add
  `std::string keyFile;` with an **empty** compiled default (not
  `~/.blackboxai/keys`). Rationale: the default is a user-home path, and baking
  it into Config would make headless auto-load a dev box's real keys file into
  the golden suite. So Config stays deterministic (empty unless the rc sets it),
  and **Server** resolves `~/.blackboxai/keys` — and only when not headless,
  exactly mirroring the rc-discovery stance.
- `Config.cc` (after the `menuFile` read):
  `cfg.keyFile = bt::expandTilde(res.read("session.keyFile", "Session.KeyFile",
  ""));` — note the `Session.KeyFile` **class** capitalisation; `expandTilde`
  (already included via `Util.hh`) still applies to an explicitly-set path.
- Read-only key ⇒ **no** `ConfigSpelling.hh` / config-menu writer changes.

### Server (`src/Server.cc`)
- Ctor, after `config_ = Config::load(rc_path_)` (`Server.cc:94`): if
  `!config_.keyFile.empty()` call `keybindings_.loadFile(config_.keyFile)` (false
  → keep default table). `keybindings_` stays the same value member
  (`Server.hh:433`) — swapping its contents is enough; all three dispatch call
  sites (`1614`, `1662`, `1913`) read the one member.
- `reconfigure()` (`Server.cc:415-430`): re-run the same `loadFile`-or-default
  step so live reconfigure re-reads bindings (today it rebuilds everything
  config-derived *except* `keybindings_`).
- `executeAction()` switch (`Server.cc:1669-1694`): add
  `case Action::Exec: commandRunner().run({"/bin/sh","-c", a.exec}); break;`.

### Data + build (`meson.build`, `data/keys`)
- Ship `data/keys` = the built-in defaults in friendly-alias form (a copyable
  reference; not auto-loaded, since builtins already equal it).
- `meson.build:99` (after the `data/menu` `install_data`): `install_data('data/keys',
  install_dir: get_option('datadir') / 'blackboxai')` → `/usr/share/blackboxai/keys`.

### Docs
- `doc/blackboxai.1.in`: add a KEYS section (grammar, action list, reserved key,
  `session.keyFile`). Optionally add a `@keysdir@` token + `man_cfg.set` in
  `doc/meson.build` to advertise the installed path.
- `README.md`: rewrite the `## Keybindings` prose (`46-50`, which asserts fixed
  bindings *and* cites the Keybindings.hh "M5" comment) and delete the limitations
  bullet (`120`).
- `src/Keybindings.hh:4-5`: remove the "M5 … hardcoded" promise (keep/​reword the
  `M4 built-in table` label on line 1).

## 7. Tests (project discipline: TDD, ~90% gcov)

- **Unit** (`tests/unit/keybindings_file_test.cc`, no `main`; add to
  `unit_sources`): `parseLine` for every modifier token + alias, keysym
  resolution, `Workspace N` → `arg=N-1`, `MoveToOutput <dir>` → direction,
  `Exec` remainder capture, malformed / comment / blank → skipped,
  case-insensitivity; `builtinDefaults()` non-empty and stable.
- **Table**: `loadFile` replace semantics; reserved `Ctrl+Alt+BackSpace → Quit`
  present *and wins* even when the file rebinds that chord; unreadable file →
  returns false and leaves defaults.
- **System** (`tests/system/keybindings_file_test.cc`, own DOCTEST main; own
  `executable()`+`test()` block per `keybinding_exe` at `tests/meson.build:197`):
  a `tests/fixtures/*.keys` fixture; `Server server(true, rc)` →
  `injectKeyForTest` drives a rebound chord → assert `lastActionForTest()` /
  `currentWorkspaceForTest()`. `Exec`: `setCommandRunnerForTest(&fake)` + inject →
  `fake.runCount()==1`, `fake.lastCommand()=={"/bin/sh","-c",cmd}` (pattern from
  `style_boot_test.cc:43-58`).
- No golden PNGs — this slice renders nothing.

## 8. Implementation phasing (two commit series)

1. **Rebind-only:** `parseLine` + `builtinDefaults` + `loadFile` (reserved key),
   `Action` struct (no `Exec` yet), Config `keyFile`, Server ctor + reconfigure
   wire-in, `data/keys`, docs, unit+table+system tests over the 14 existing
   actions.
2. **`Exec` on top:** `Action::Exec` + `exec` payload, `executeAction` Exec case
   routed through `commandRunner()`, `:Exec` in `parseLine`, its unit + system
   tests.

## 9. Out of scope

Per-application/window-context bindings, mouse-button bindings, chained key
sequences, an in-menu keybinding editor, and reading real `~/.bbkeysrc` grammar.
Keybindings are not written back through the Configuration menu (read-only key).

## 10. Acceptance

1. Existing user, no keys file → identical bindings to today (builtins).
2. A keys file rebinds a chord; `injectKeyForTest` proves the new mapping;
   built-in default for that chord no longer fires.
3. `Ctrl+Alt+BackSpace → Quit` fires even when the file rebinds that chord.
4. `:Exec` records `{"/bin/sh","-c",cmd}` through the fake runner.
5. `reconfigure()` picks up an edited keys file live.
6. CI green in `blackboxai-ci:f44`; coverage gate holds (~90%); `data/keys`
   installs to `/usr/share/blackboxai/keys`; man page + README updated.

## Build/test note

Build and test **only** inside `blackboxai-ci:f44`; the host has no wlroots.
Docker needs `--shm-size` headroom (default 64 MB SIGBUSes the pixman system
tests): `docker run --rm --shm-size=4g -v <repo>:/src -w /src blackboxai-ci:f44`.
