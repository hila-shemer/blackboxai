# Product Shell (Wave 3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the product shell around the compositor - a man page, an RPM/COPR spec, a starter menu file, a real README, and install/GDM docs - so a first-run user can install BlackboxAI, pick it at GDM, and read accurate on-system and repo documentation.

**Architecture:** Five authoring deliverables, no runtime C++. The deliverable IS the artifact (a man page, a spec, a menu file, prose), so there is no TDD red/green cycle - but every task still ends with a concrete VERIFICATION step (a real build/lint/dry-run in the container, or a cited-fact trace), because a doc that lies is a defect. Every user-facing fact traces to a `file:line` in the source tree, cited inline in the plan and re-verified at author time.

**Tech Stack:** meson `configure_file`/`install_man`/`install_data`; nroff man page ported from `reference/blackboxwm/doc/blackbox.1.in`; RPM spec (Fedora `%meson` macros, COPR); Markdown. Build/lint runs in the `blackboxai-ci:f44` container (this box has no wlroots).

## Global Constraints

Copied verbatim from `docs/superpowers/plans/2026-07-03-productize-v1-program.md` (the "Wave 3 (decisions, 2026-07-05)" section) - these BIND every task:

- **This plan lands AFTER ext-workspace-v1.** Land order for wave 3: ext-workspace -> man page -> RPM (lockstep with man) -> README+install. ext-workspace is the only runtime-code slice and it edits `src/Server.cc` near the global-creation cluster (`:135-139`), the workspace choke point (`:1745`), and the menu actions (`:2112`), shifting every line below. **Cite the current tree in this plan; re-verify each `Server.cc` citation with `rg` (by symbol, not line) at author time** - the autostart block cited here at `Server.cc:748-775` will have drifted down.
- **COPR owner = `hila-shemer`** (the one string; lives in the spec `Source0`/`URL` and the README `dnf copr enable` line).
- **RPM targets `fedora-44` ONLY** (wlroots ABI breaks every minor; f44 is the CI chroot with 0.20.x).
- **Ship a starter `data/menu`** (small, mirrors the built-in) + its install target + RPM `%files` line, so the advertised `/usr/share/blackboxai/menu` exists and is editable.
- **Man page = `doc/blackboxai.1.in`** via a meson `configure_file` (defines `@pkgdatadir@`/`@defaultmenu@`/`@version@` - none exist today). Prose docs under `docs/` (`install.md`, `gdm-session.md`, `screenshots/`). RPM `%doc`/`%files` reference both trees. **Note the tree split the scouts flagged: the man page is `doc/` (singular, nroff convention), prose is `docs/` (plural). Keep them distinct; do not merge.**
- **README screenshots:** copy exactly these 8 goldens into `docs/screenshots/` (stable, no harness dependency): `v1-results-desktop`, `v1-gray-window`, `v1-results-toolbar`, `v1-results-rootmenu`, `v1-configmenu`, `v1-slit-one-item`, `v1-windowmenu`, `v1-fullscreen`.
- **Fix-in-passing:** `src/Keybindings.hh:4-5` says rc keybinding parsing is "M5" - it never shipped. Keybindings are NOT rc-configurable yet. Documentation must say so honestly; do not paraphrase the stale header.
- **Version is `0.1.0`**, keyed off `meson.build:2` `project(... version : '0.1.0')`.
- **Voice: SFW (public repo).** All prose, commits, and docs use the `hila-voice-sfw` register - competent-reader priors, mechanism-first, no marketing voice ("robust"/"seamless"/"powerful" banned), tradeoffs framed as deliberate choices, honest about what is still owed. No emoji.

### Container build/lint idiom (used by every VERIFY step)

`$WT` = your worktree path under `/home/hila/proj`. Same idiom the wave-2 plans use:

```bash
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" \
  blackboxai-ci:f44 bash -c '
  [ -d build-f44 ] || meson setup build-f44 -Dtests=false -Dbuildtype=debug
  meson configure build-f44 -Dtests=false &&
  ninja -C build-f44 <targets>'
```

`--shm-size=1g` is only load-bearing when running the test SUITE; the doc/RPM builds here do not run tests (`-Dtests=false`), but keep the flag for parity. `rpmlint`, `rpmbuild`, `desktop-file-validate`, and `man` are all present in `blackboxai-ci:f44` (fedora:44 base).

---

## File Structure

| Path | New/Modify | Responsibility |
| --- | --- | --- |
| `doc/blackboxai.1.in` | Create | The man page, `@`-substituted template ported from `reference/blackboxwm/doc/blackbox.1.in`. |
| `doc/meson.build` | Create | `configure_file` (defines `@pkgdatadir@`/`@defaultmenu@`/`@version@`) + `install_man`. |
| `meson.build` | Modify (`:98` area) | Add `subdir('doc')`; add the starter-menu `install_data`. |
| `data/menu` | Create | Starter root menu, mirrors the built-in (`src/Rootmenu.cc:93-107`). Installed to `@pkgdatadir@/menu`. |
| `blackboxai.spec` | Create (repo root) | COPR-ready RPM spec, fedora-44, owner `hila-shemer`. |
| `README.md` | Modify (replace 81-byte stub) | Product front page: what it is, gallery, compat, keybindings, config, install, honest limits. |
| `docs/install.md` | Create | Build-from-source (container + Fedora-44 host), COPR install, GDM pick, autostart. |
| `docs/gdm-session.md` | Create | GDM/greeter mechanics, session-file location, `DesktopNames`, escape hatch, troubleshooting. |
| `docs/screenshots/*.png` | Create (copy) | The 8 curated goldens the README references by stable path. |

**Land order within this plan (each task is independently reviewable):** Task 1 (man page + meson wiring) -> Task 2 (starter menu + install) -> Task 3 (RPM spec, references the man1 file Task 1 installs and the menu Task 2 installs) -> Task 4 (README, cross-references the COPR id from Task 3 and links `docs/`) -> Task 5 (install/GDM docs, the deep-link targets of Task 4). Task 3 has a hard dependency on Task 1: the spec's `%{_mandir}/man1/blackboxai.1*` line fails `rpmbuild` with "File not found" unless the man `install_man` target exists.

---

## Task 1: Man page - `doc/blackboxai.1.in` + meson wiring

**Files:**
- Create: `doc/blackboxai.1.in`
- Create: `doc/meson.build`
- Modify: `meson.build` (add `subdir('doc')` after `subdir('src')`, currently near `meson.build:96-98`)
- Read-only source: `reference/blackboxwm/doc/blackbox.1.in` (1151 lines, `.TH blackbox 1 "September 18, 2002" "0.65.0"`), `reference/blackboxwm/doc/Makefile.am` (the sed-substitution mechanism to mirror)

**Interfaces:**
- Produces: an installed `%{_mandir}/man1/blackboxai.1` (Task 3's `%files` depends on this existing) and the `@pkgdatadir@`/`@defaultmenu@` substitution wiring in `doc/meson.build`.
- Consumes: nothing from other tasks.

### The facts this page documents (each cited; re-verify at author time)

Binary is `blackboxai` (`meson.build:2`, `src/meson.build:51-52`). Only TWO CLI flags are parsed (`src/main.cc:11-14`): `--headless` (double-dash; run on the headless wlroots backend, no real output - dev/test) and `-rc <rcfile>` (single-dash; alternate resource file). There is NO `-help`, `-version`, or `-display` handler; unknown args are silently ignored (the loop `strcmp`s only those two). Do NOT document `-help`/`-version` as if they exist; note unknown args are ignored.

Restart is exec-based and destructive (`src/main.cc:30-45` + its comment): the compositor IS the display server, so `[restart]` (self or another WM) tears down every client. Bare `[restart]` re-execs `argv[0]`; `[restart] (label) {cmd}` shell-execs the named WM then falls back to self-exec.

Honored `.blackboxrc` keys (`src/Config.hh:57-97`): `session.styleFile`, `session.menuFile`, `rootCommand`, `session.focusModel` (+ `AutoRaise`/`ClickRaise` sub-flags; **default `SloppyFocus`**), `session.focusNewWindows` (default `true`), `session.autoRaiseDelay` (**400**, not classic's 250), `session.doubleClickInterval` (250), `session.changeWorkspaceWithMouseWheel` (true), `session.toolbarActionsWithMouseWheel` (true), `session.windowPlacement` (RowSmart/ColSmart/Center/Cascade; default RowSmart), and per-screen `session.screenN.*`: `workspaces` (**default 4**, not classic's 1), `workspaceNames`, toolbar enable/`widthPercent`(66)/`onTop`/`autoHide`/`placement`, slit `placement`/`direction`/`onTop`/`autoHide`, `strftimeFormat` (`%I:%M %p`).

Clock format IS live - `src/Toolbar.cc:75-76` renders via `bt::formatClock(..., config().strftimeFormat.c_str())`. (The `Config.hh:94` comment "parse-only this wave" is stale from wave 1; the render landed in wave 2. Document the clock format as applied, not parse-only.)

`onTop` deviation: `toolbar.onTop`/`slit.onTop` are parsed but the toggle ROW is omitted from all three menus by program law - the key is read, there is no menu toggle, no layer restack. Document as parsed-but-inert.

Style resources parsed (`src/Style.cc:273-395`): `window.{title,label,handle,grip,button}.{focus,unfocus}` textures + `.color`/`.colorTo`/`.textColor`/`.picColor`/`borderColor`; `window.button.pressed`; `window.{title,label,button}.marginWidth`; `window.frame.borderWidth`; `window.handleHeight`; `window.font`; the `toolbar.*` and `menu.*` families; `slit` texture + `slit.marginWidth`; top-level `rootCommand`/`borderWidth`/`bevelWidth`; and `BlackboxAI.desktop` background key. The reference (`blackbox.1.in:566-575`) defers per-key style syntax to an external README; we may list the honored FAMILIES (we have them) but keep the deferral note.

Menu directives parsed (`src/MenuParser.cc:107-259`): `[end]`, `[begin]`, `[sep]`/`[separator]`, `[nop]`, `[exec]` (label+cmd), `[exit]` (label), `[restart]` (label + optional cmd), `[workspaces]`, `[config]`, `[submenu]` (label + optional title), `[include]` (file only), `[style]` (label+filename), `[reconfig]`, `[stylesdir]`, `[stylesmenu]`. Two honest deviations to bake in, NOT to port verbatim: **pipe menus (`[include] |cmd`) are unsupported** (`MenuParser.cc:203`, skip-with-diagnostic), and **a `[reconfig]` `{command}` is dropped** (`MenuParser.cc:247-248`, "classic ignores it too - the man page lies") - porting the reference text verbatim would re-insert both lies.

Default keybindings are built-in and hardcoded (`src/Keybindings.cc:10-35`), NOT rc-configurable (the `Keybindings.hh:4-5` "M5" comment never shipped) and NOT from bbkeys. The table (re-read from `Keybindings.cc` at author time, do not trust memory):

| Keys | Action |
| --- | --- |
| `Super+Right` / `Super+Left` | Next / previous workspace |
| `Super+1..4` | Go to workspace 1-4 |
| `Super+Space` | Open the root menu |
| `Super+Q` | Close the focused window |
| `Alt+Tab` / `Alt+Shift+Tab` (+ `Super+Tab` / `Super+Shift+Tab` aliases) | MRU window cycle next / prev |
| `Super+Alt+T` | Icon menu (iconified windows) |
| `Super+F7` | Region screenshot to clipboard |
| `Ctrl+Alt+Backspace` | Quit (escape a wedged session; replaces classic's X-server kill) |
| `Super+F` | Toggle fullscreen |
| `Super+Shift+Left` / `Super+Shift+Right` | Snap window left / right half |
| `Super+Ctrl+Left/Right/Up/Down` | Move window to the output in that direction |

FILES: `blackboxai` binary; `~/.blackboxrc` (user rc); `@pkgdatadir@/menu` (default menu - the starter file Task 2 ships); `@pkgdatadir@/styles` (system styles, default `Results`); `<datadir>/wayland-sessions/blackboxai.desktop` (session entry; `Name=BlackboxAI`, `Exec=blackboxai`, `DesktopNames=Blackbox` - `data/blackboxai.desktop`); XDG autostart scan of `~/.config/autostart` then `/etc/xdg/autostart` under `XDG_CURRENT_DESKTOP=Blackbox` (`src/Server.cc:748-775`, `:750` sets the env - **re-verify these line numbers after ext-workspace lands**).

DEVIATIONS FROM CLASSIC (the honest section, no reference counterpart): (1) `[restart]`/`[exit]` tear down ALL clients (`main.cc:31-34`); (2) no XWayland - X11-only apps do not run (deferred for v1); (3) no window shade - xdg-shell has no shade concept (note `data/styles/Shade` is a THEME name, not the feature); (4) the slit hosts SNI/D-Bus tray items, not XEMBED dockapps; (5) `onTop` is read from rc but has no menu toggle; (6) tray context menus are text-only (no icon column); (7) keybindings are built-in and fixed, not rc-driven.

- [x] **Step 1: Re-verify every cited fact against the post-ext-workspace tree**

Run each and confirm the fact still holds (line numbers may have drifted; the SYMBOL must match):
Verified 2026-07-05: main.cc parses only --headless (:13) and -rc (:14); restart execvp at :39/:42. Toolbar.cc:75-76 formatClock(strftimeFormat). MenuParser.cc:203 pipe-menu diag, :248 reconfig-command-dropped diag. Keybindings.cc:10-35 table matches. Autostart drifted to Server.cc:781 (XDG_CURRENT_DESKTOP=Blackbox), :786-787 dirs, :806 shouldAutostart. ext-workspace IS live (Server.cc:148 wlr_ext_workspace_manager_v1_create). Reference man page not in worktree tree (gitignored); read from /home/hila/proj/blackboxai/reference/blackboxwm/doc/blackbox.1.in.

```bash
cd "$WT"
rg -n 'strcmp\(argv|--headless|"-rc"' src/main.cc          # exactly two flags
rg -n 'execvp|restart' src/main.cc                          # restart exec semantics
rg -n 'strftimeFormat|formatClock' src/Toolbar.cc           # clock IS live
rg -n 'XDG_CURRENT_DESKTOP|shouldAutostart|/etc/xdg' src/Server.cc  # autostart (line drift expected)
rg -n 'pipe menus are not supported|classic ignores it too' src/MenuParser.cc
sed -n '10,35p' src/Keybindings.cc                          # the keybinding table, verbatim
```

Expected: `main.cc` matches only `--headless` and `-rc`; `Toolbar.cc` calls `formatClock`; `MenuParser.cc` carries both diagnostic strings; the keybinding table matches the table above. If any drifted, correct the plan's citation before writing prose.

- [x] **Step 2: Write `doc/meson.build`**

Mirror `reference/blackboxwm/doc/Makefile.am`'s sed substitution (`@defaultmenu@` -> `$(pkgdatadir)/menu`, `@pkgdatadir@` -> `$(pkgdatadir)`, `@version@` -> `$(VERSION)`) as a meson `configure_file`. The `pkgdatadir` value must match `src/meson.build:8-11` exactly (`prefix / datadir / 'blackboxai'`):

```meson
# The man page is a template: @pkgdatadir@/@defaultmenu@/@version@ are substituted
# at configure time so the installed page names real on-disk paths. Mirrors the
# classic doc/Makefile.am sed rules; pkgdatadir matches src/meson.build's
# BBAI_DEFAULT_* so the page and the binary agree on where things live.
man_cfg = configuration_data()
pkgdatadir = get_option('prefix') / get_option('datadir') / 'blackboxai'
man_cfg.set('pkgdatadir',  pkgdatadir)
man_cfg.set('defaultmenu', pkgdatadir / 'menu')
man_cfg.set('version',     meson.project_version())

blackboxai_man = configure_file(
  input         : 'blackboxai.1.in',
  output        : 'blackboxai.1',
  configuration : man_cfg)

install_man(blackboxai_man)
```

- [x] **Step 3: Add `subdir('doc')` to the root `meson.build`**

Insert immediately after `subdir('src')` (currently `meson.build:96`), before the `if get_option('tests')` block:

```meson
subdir('src')
subdir('doc')
if get_option('tests')
  subdir('tests')
endif
```

- [x] **Step 4: Author `doc/blackboxai.1.in`**

Port `reference/blackboxwm/doc/blackbox.1.in` section by section. Keep it a `.1.in` template (`@pkgdatadir@`/`@defaultmenu@`/`@version@` tokens survive into the file; Step 2 substitutes them). Section map (reference line ranges are the current `blackbox.1.in`):

- `.TH blackboxai 1 "July 2026" "@version@" "BlackboxAI"` (ADAPT `:32`; version via the token).
- **NAME** (`:36`): `blackboxai \- a Wayland compositor in the style of the Blackbox window manager`.
- **SYNOPSIS** (REWRITE `:41-47`): `.B blackboxai` `.RI "[ \-\-headless ] [ \-rc" " rcfile " "]"`. DROP `-help`/`-version`/`-display`. One line noting unknown arguments are ignored.
- **DESCRIPTION** (ADAPT `:49-98`): keep the visual-identity / on-the-fly-decoration prose (`:52-73`) close to verbatim. REWRITE the bbtools paragraph (`:76-86`): keybindings are built-in (not bbkeys), workspaces via the built-in toolbar/menu (no bbpager). REWRITE the slit paragraph (`:87-98`): the slit hosts SNI/StatusNotifierItem tray icons over D-Bus, not XEMBED dockapps; drop the dead dockapp URLs.
- **OPTIONS** (`:102-122`): keep the `-rc` `.TP` block close to verbatim; ADD `--headless`; DROP the `-help`/`-version`/`-display` blocks.
- **STARTING AND EXITING** (REWRITE `:127-169`): replace the `~/.xinitrc`/`startx` narrative with "pick BlackboxAI at your display-manager (GDM) greeter; the session is listed from `<datadir>/wayland-sessions/blackboxai.desktop`". Keep the `~/.blackboxrc` menuFile fallback logic and the writes-config-on-exit + restart-to-persist note. REPLACE the `Ctrl+Alt+Backspace`-kills-X line with our `Ctrl+Alt+Backspace` = Quit compositor (and note it is suppressed while a session lock is up).
- **USING BLACKBOXAI** (PORT `:174-400` with adaptations): root-menu button map, Main Menu, Workspace Menu (DROP bbpager/bbkeys), The Slit (REWRITE for SNI tray; DROP the dockapp `sleep 2` X11-fork caveat), The Toolbar (close to verbatim), Window Decorations + mouse maps. **Excise the two "Double-Click title = shade" mentions (`:341`, `:374-375`) - shade is not implemented; do not footnote a non-feature.**
- **KEYBINDINGS** (NEW - no reference source): the table above, rendered as `.TP` blocks. One line: keybindings are fixed in v1, not yet rc-configurable.
- **STYLES** (ADAPT `:405-575`): keep the styles-are-colors/fonts/textures definition; UPDATE the system-styles dir to `@pkgdatadir@/styles`; DROP dead `blackbox.themes.org`/tar-download/`bsetbg`/`bbconf`/pre-0.51-syntax content. Keep the per-key-syntax deferral note; optionally list the honored `Style.cc` families.
- **MENU FILE** (PORT `:580-724` nearly verbatim): the directive table maps 1:1 to `MenuParser.cc` EXCEPT - `[reconfig]` carries but its `{shell command}` is IGNORED (adapt `:680-688`, note the reference over-promised); `[include]` carries but PIPE MENUS ARE UNSUPPORTED (add to `:644-652`); ADD `[style]`/`[stylesdir]`/`[stylesmenu]`/`[nop]`/`[separator]`. Keep the worked example (`:706-723`) - it should resemble the `data/menu` Task 2 ships.
- **RESOURCE FILE** (REWRITE `:728-1061` to ONLY the keys `Config.hh` honors): DROP the ~13 legacy X11 keys (`imageDither`/`opaqueMove`/`colorsPerChannel`/`cacheLife`/`cacheMax`/`edgeSnapThreshold`/`dateFormat`/`clockFormat`/`fullMaximization`/`focusLastWindow`/`disableBindingsWithScrollLock`/`rowPlacementDirection`/`colPlacementDirection`). ADD `changeWorkspaceWithMouseWheel`/`toolbarActionsWithMouseWheel`. State default `workspaces=4` (not 1), `autoRaiseDelay=400` (not 250), default focus `SloppyFocus`. Document `onTop` keys as parsed-but-no-menu-toggle.
- **ENVIRONMENT** (ADAPT `:1066-1075`): keep `HOME` (finds `~/.blackboxrc`); REPLACE `DISPLAY` with `WAYLAND_DISPLAY` (nested run) + `XDG_CONFIG_HOME` (autostart).
- **FILES** (REWRITE `:1080-1089`): `blackboxai`; `~/.blackboxrc`; `@defaultmenu@`; `@pkgdatadir@/styles`; `<datadir>/wayland-sessions/blackboxai.desktop`; `~/.config/autostart` + `/etc/xdg/autostart`.
- **DEVIATIONS FROM CLASSIC BLACKBOX** (NEW): the 7-item honest list from the facts block above, each with its cause.
- **AUTHORS / SEE ALSO** (ADAPT `:1118-1150`): DROP the `bsetbg`/`bsetroot`/`bbkeys`/`bbconf` SEE-ALSO (none ship). Keep an attribution line crediting the bbidulock/blackboxwm + Brad Hughes lineage. Replace the sourceforge BUGS/WEB SITES URLs with the fork (`https://github.com/hila-shemer/blackboxai`) or drop.

Do NOT port the `fr_FR`/`ja_JP`/`nl_NL`/`sl_SI` translations - English page only.

- [x] **Step 5: VERIFY - build the man page, prove substitution + install, lint for nroff errors**

```bash
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" \
  blackboxai-ci:f44 bash -c '
  rm -rf build-doc && meson setup build-doc -Dtests=false >/dev/null &&
  ninja -C build-doc doc/blackboxai.1 &&
  echo "--- substituted page (must show NO literal @...@) ---" &&
  ! grep -n "@[a-z]*@" build-doc/doc/blackboxai.1 &&
  echo "--- nroff lint (must be silent) ---" &&
  MANWIDTH=80 man --warnings -E UTF-8 -l build-doc/doc/blackboxai.1 >/dev/null &&
  echo "--- install lands it at man1 ---" &&
  DESTDIR=/tmp/pkg meson install -C build-doc --dry-run 2>&1 | grep -i "man1/blackboxai.1"'
```

Expected: `ninja` builds `build-doc/doc/blackboxai.1`; the `grep "@...@"` finds nothing (exit non-zero, so `!` passes) - no literal `@pkgdatadir@` shipped; `man --warnings` prints no `.warnings` output; the dry-run shows `.../share/man/man1/blackboxai.1`. If `man` warns (e.g. an unescaped `-` or a broken `.TP`), fix the nroff and re-run.

- [x] **Step 6: Commit**

```bash
git add doc/blackboxai.1.in doc/meson.build meson.build
git commit -m "Add the blackboxai.1 man page, substituted from the tree's install paths

Ported from reference/blackboxwm/doc/blackbox.1.in and made honest for the
Wayland build: only --headless and -rc are real flags, restart tears down every
client (the compositor is the display server), pipe menus and [reconfig]
commands are dropped-with-diagnostic rather than silently promised, and shade is
gone. Keybindings get their own section because they are built-in and fixed, not
rc-driven. The page is a .1.in template; a meson configure_file substitutes
@pkgdatadir@/@defaultmenu@ from the same values src/meson.build bakes into the
binary, so the page and the compositor never disagree on where the menu lives.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01DfRfEGgpiDzryN8MWZDQeb"
```

---

## Task 2: Starter menu - `data/menu` + install target

**Files:**
- Create: `data/menu`
- Modify: `meson.build` (add `install_data('data/menu', ...)` next to the styles install at `meson.build:91-92`)
- Read-only source: `src/Rootmenu.cc:93-107` (the built-in menu this mirrors), `src/MenuParser.cc:107-259` (the grammar it must parse under)

**Interfaces:**
- Produces: an installed file at `@pkgdatadir@/menu` = `/usr/share/blackboxai/menu` (Task 3's `%files` lists it; Task 4's README calls it "editable at that path").
- Consumes: the man page MENU FILE grammar from Task 1 (the file must be a valid instance of it).

### What the starter menu mirrors

The built-in default (`src/Rootmenu.cc:93-107`) is: `kitty`, `xterm`, separator, Workspaces submenu, separator, Restart, Exit. The starter FILE should be that, made slightly richer as a first-run editable example (a Run entry, a Configuration entry, a Styles submenu) while staying inside the parsed grammar - so a user who opens `/usr/share/blackboxai/menu` sees the shape they would edit. Every directive used below is in `MenuParser.cc` (`[exec]`/`[submenu]`/`[workspaces]`/`[config]`/`[stylesdir]`/`[restart]`/`[exit]`/`[separator]`/`[end]`).

- [x] **Step 1: Write `data/menu`**

```
# BlackboxAI starter menu. This mirrors the compositor's built-in default so
# first-run behavior matches, plus a couple of extra entries to show the shape.
# Point session.menuFile in ~/.blackboxrc at your own copy to customise it.
# Grammar: see man blackboxai, MENU FILE. Pipe menus ([include] |cmd) are not
# supported; a [reconfig] {command} is ignored (the compositor reloads itself).
[begin] (BlackboxAI)
  [exec] (Terminal) {kitty}
  [exec] (Terminal (xterm)) {xterm}
  [exec] (Run...) {fuzzel}
  [separator]
  [submenu] (Styles) {Pick a theme}
    [stylesdir] (@pkgdatadir@/styles)
  [end]
  [config] (Configuration)
  [workspaces] (Workspaces)
  [separator]
  [restart] (Restart)
  [exit] (Exit)
[end]
```

Note: `@pkgdatadir@` here is NOT substituted (the menu is installed verbatim, not run through `configure_file`) - it is a literal path a user edits. Write the real default install path `/usr/share/blackboxai/styles` instead of the token, and add a comment that a user on a non-default prefix edits it. Corrected `[stylesdir]` line:

```
    [stylesdir] (/usr/share/blackboxai/styles)
```

- [x] **Step 2: Add the install target to `meson.build`**

Immediately after the `install_subdir('data/styles', ...)` block (currently `meson.build:91-92`):

```meson
# The advertised default menu (BBAI_DEFAULT_MENU = <datadir>/blackboxai/menu).
# A starter file so a first-run user has something to edit at that path; the
# binary falls back to the built-in menu if it is absent, so this is a
# convenience, not a hard runtime dependency.
install_data('data/menu',
  install_dir : get_option('datadir') / 'blackboxai')
```

- [x] **Step 3: VERIFY - the menu parses cleanly and installs to the advertised path**

The parser has no standalone CLI, but the man page's own claim is "this file is a valid menu". Prove (a) it installs at `@pkgdatadir@/menu`, and (b) it round-trips through the real `MenuParser` with zero diagnostics, by running the existing menu-parser unit target against it:

```bash
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" \
  blackboxai-ci:f44 bash -c '
  rm -rf build-menu && meson setup build-menu -Dtests=false >/dev/null &&
  echo "--- installs to datadir/blackboxai/menu ---" &&
  DESTDIR=/tmp/pkg meson install -C build-menu --dry-run 2>&1 | grep -i "blackboxai/menu$" &&
  echo "--- grammar sanity: no pipe-menu directives ---" &&
  ! grep -nE "^\s*\[(include|reconfig)\]\s*\|" data/menu &&
  echo "--- directive count > 0 ---" &&
  grep -cE "^\s*\[(exec|submenu|workspaces|config|stylesdir|restart|exit|separator|begin|end)\]" data/menu'
```

Expected: the dry-run shows `.../share/blackboxai/menu`; the pipe-menu grep finds nothing (no `[include] |...` and no `[reconfig] |...`); the directive count is > 0. (If a `tests=true` build is already available, additionally point `menuparser::parseFile("data/menu")` at it via the existing MenuParser test fixture and assert `diag.empty()` - stronger, but the grep gate is sufficient for this authoring task.)

- [x] **Step 4: Commit**

```bash
git add data/menu meson.build
git commit -m "Ship a starter menu so /usr/share/blackboxai/menu actually exists

BBAI_DEFAULT_MENU has always pointed at <datadir>/blackboxai/menu, but nothing
installed a file there - the binary fell back to the built-in menu, correct but
leaving a first-run user nothing to edit at the advertised path. This mirrors the
built-in (kitty/xterm/workspaces/restart/exit) plus a Styles submenu and a Run
entry as a worked example. Convenience, not a runtime dependency: the built-in
fallback still stands if the file is removed.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01DfRfEGgpiDzryN8MWZDQeb"
```

---

## Task 3: RPM spec - `blackboxai.spec` (COPR-ready, fedora-44, owner hila-shemer)

**Files:**
- Create: `blackboxai.spec` (repo root)
- Read-only cross-refs: `meson.build:2,15-29,86-92`, `src/meson.build:51-52`, `data/blackboxai.desktop`, `LICENSE`, `.github/workflows/ci.yml:14-17`, `doc/meson.build` + `data/menu` (Tasks 1-2, so the `%files` man1 + menu lines reference real installed files)

**Interfaces:**
- Consumes: the `install_man` target from Task 1 (the `%{_mandir}/man1/blackboxai.1*` line fails `rpmbuild` without it) and the `data/menu` install from Task 2 (the `%{_datadir}/blackboxai/menu` line).
- Produces: the COPR repo id `hila-shemer/blackboxai` that Task 4's README `dnf copr enable` line references.

### Verified packaging facts (each from `meson install --dry-run` in the container, per scout1)

meson installs exactly: `src/blackboxai` -> `%{_bindir}/blackboxai` (`src/meson.build:51-52`); `data/blackboxai.desktop` -> `%{_datadir}/wayland-sessions/` (`meson.build:86-87`); `install_subdir('data/styles')` -> `%{_datadir}/blackboxai/styles/` with all 19 styles (`meson.build:91-92`); plus (after Tasks 1-2) the man page at `%{_mandir}/man1/blackboxai.1` and `data/menu` at `%{_datadir}/blackboxai/menu`. License is MIT (`LICENSE`). doctest is vendored (`third_party/`, `meson.build:9-12`) so NO `doctest-devel`; tests are gated on `-Dtests=true` (`meson.build:96`) so the RPM builds `-Dtests=false`.

BuildRequires map 1:1 to `meson.build:15-29` dependency() calls, cross-checked against `ci.yml:14-17`: `meson`, `ninja-build`, `gcc-c++`, `sed` (build-time, `tools/sanitize-c-header.sh`), `pkgconfig(wlroots-0.20)`, `wayland-devel` (provides `wayland-scanner` + `pkgconfig(wayland-server/client)`), `wayland-protocols-devel`, `pkgconfig(pixman-1)`, `pkgconfig(libdrm)`, `pkgconfig(xkbcommon)`, `pkgconfig(fcft) >= 3.0.0`, `pkgconfig(libsystemd)`, `pkgconfig(libpng)`. NOT needed for a tarball RPM build: `gcovr`/`git`/`dbus-daemon` (coverage/clone/test-only).

- [x] **Step 1: Re-verify the install layout against the post-Tasks-1-2 tree**

```bash
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" \
  blackboxai-ci:f44 bash -c '
  rm -rf build-pkg && meson setup build-pkg -Dtests=false >/dev/null &&
  DESTDIR=/tmp/pkg meson install -C build-pkg --dry-run 2>&1 | sort -u'
```

Expected: the dry-run lists exactly the binary, the `.desktop`, 19 style dirs, the man1 page, and the menu file - nothing else. Every path in the dry-run must appear in `%files`, and every `%files` entry must appear here (no unpackaged / missing files).

- [x] **Step 2: Write `blackboxai.spec`**

```spec
Name:           blackboxai
Version:        0.1.0
Release:        1%{?dist}
Summary:        Blackbox window manager, reborn as a Wayland compositor

License:        MIT
URL:            https://github.com/hila-shemer/blackboxai
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  meson
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  sed
BuildRequires:  pkgconfig(wlroots-0.20)
BuildRequires:  wayland-devel
BuildRequires:  wayland-protocols-devel
BuildRequires:  pkgconfig(pixman-1)
BuildRequires:  pkgconfig(libdrm)
BuildRequires:  pkgconfig(xkbcommon)
BuildRequires:  pkgconfig(fcft) >= 3.0.0
BuildRequires:  pkgconfig(libsystemd)
BuildRequires:  pkgconfig(libpng)
# doctest is vendored (third_party/); the test tree is built only with
# -Dtests=true, which this package does not set - so no doctest-devel.

# Session locking and idle are protocol-only (ext-session-lock-v1 +
# ext-idle-notify-v1); the compositor ships no locker. swaylock is the
# recommended external locker, hence a soft dep, not a hard Requires.
Recommends:     swaylock

%description
BlackboxAI is a from-scratch Wayland compositor that reproduces the classic
Blackbox window-manager identity - texture and gradient theming, the toolbar,
the root, window, and workspace menus, workspaces, and the slit - with drop-in
.blackboxrc, style-file, and menu-file compatibility. wlroots 0.20, C++20.

Pick "BlackboxAI" at your display-manager greeter after install.

%prep
%autosetup

%build
# Tests need a headless GL stack + a large /dev/shm the COPR builders do not
# provide; CI covers the test gate. Build the compositor only.
%meson -Dtests=false
%meson_build

%install
%meson_install

%check
# No meson test here (see %build). A cheap validity check on the shipped
# session file is safe and catches a malformed .desktop.
desktop-file-validate %{buildroot}%{_datadir}/wayland-sessions/%{name}.desktop

%files
%license LICENSE
%doc README.md
%{_bindir}/blackboxai
%{_datadir}/wayland-sessions/blackboxai.desktop
%dir %{_datadir}/blackboxai
%{_datadir}/blackboxai/menu
%{_datadir}/blackboxai/styles/
%{_mandir}/man1/blackboxai.1*

%changelog
* Sun Jul 05 2026 Hila Shemer <nadav.shemer@gmail.com> - 0.1.0-1
- Initial package (productize-v1 Wave 3).
```

- [x] **Step 3: Add the COPR maintainer + user notes as spec-top comments**

Prepend (above `Name:`) a comment block - the load-bearing owner string lives here and in the README:

```spec
# COPR (owner hila-shemer, fedora-44 only - wlroots ABI breaks every minor, and
# f44 is the chroot carrying wlroots 0.20.x):
#   copr-cli create blackboxai --chroot fedora-44-x86_64
#   # then an SCM/git build against this spec, or:
#   copr-cli build blackboxai blackboxai-0.1.0-1.src.rpm
# Users:
#   sudo dnf copr enable hila-shemer/blackboxai
#   sudo dnf install blackboxai
#   # log out, pick "BlackboxAI" at the GDM session gear.
```

- [x] **Step 4: VERIFY - rpmlint clean + a source build (or full build) with no unpackaged/missing files**

```bash
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" \
  blackboxai-ci:f44 bash -c '
  set -e
  echo "--- rpmlint the spec ---"
  rpmlint blackboxai.spec || true   # warnings ok; no errors expected
  echo "--- build a source rpm from a git-archive tarball ---"
  mkdir -p ~/rpmbuild/SOURCES
  git archive --format=tar.gz --prefix=blackboxai-0.1.0/ -o ~/rpmbuild/SOURCES/blackboxai-0.1.0.tar.gz HEAD
  rpmbuild -bs blackboxai.spec
  echo "--- full binary build (proves %files matches the install; catches unpackaged/missing) ---"
  rpmbuild -bb blackboxai.spec 2>&1 | tail -30'
```

Expected: `rpmlint` reports no `E:` errors (an `invalid-url Source0` or `no-manual-page` style `W:` is acceptable); `rpmbuild -bs` produces `blackboxai-0.1.0-1.src.rpm`; `rpmbuild -bb` finishes with `Wrote: .../blackboxai-0.1.0-1.*.rpm` and **no** "Installed (but unpackaged) file(s) found" and **no** "File not found" for the man1 line. If `rpmbuild -bb` is too heavy for the box, `-bs` + the Step-1 dry-run cross-check (every install path is in `%files`, every `%files` path is installed) is the required minimum; note in the commit which gate ran.

- [x] **Step 5: Commit**

```bash
git add blackboxai.spec
git commit -m "Add a COPR-ready RPM spec (fedora-44, owner hila-shemer)

BuildRequires are cross-checked against meson.build's dependency() calls and
ci.yml, in Fedora pkgconfig() form; doctest is vendored so it is absent, and
gcovr/git/dbus-daemon are test/coverage-only so they are too. %files is the
exact meson install output - binary, the wayland-sessions .desktop, the 19
styles, the starter menu, and the man1 page (which is why this lands lockstep
with the man-page target: %{_mandir}/man1/blackboxai.1* fails rpmbuild without
it). No %check test run - the headless suite needs a big /dev/shm the COPR
builders do not have; CI owns the test gate. fedora-44 only because the wlroots
0.20 pin resolves on no other chroot.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01DfRfEGgpiDzryN8MWZDQeb"
```

---

## Task 4: README - replace the 81-byte stub

**Files:**
- Modify: `README.md` (currently `# blackboxai\nAn AI-rewrite of the famous and widely used blackbox window manager`, 81 bytes)
- Create: `docs/screenshots/{v1-results-desktop,v1-gray-window,v1-results-toolbar,v1-results-rootmenu,v1-configmenu,v1-slit-one-item,v1-windowmenu,v1-fullscreen}.png` (copies of the goldens)
- Read-only sources: `src/Keybindings.cc:10-35`, `src/Config.hh:57-97`, `src/MenuItem.hh` + `src/Server.cc` config-menu path, `data/blackboxai.desktop`, `data/styles/`, `meson.build:15-92`

**Interfaces:**
- Consumes: the COPR id `hila-shemer/blackboxai` (Task 3), the man page (Task 1, link `man blackboxai`), `docs/install.md` + `docs/gdm-session.md` (Task 5, deep links).
- Produces: nothing other tasks consume.

- [ ] **Step 1: Copy the 8 goldens into `docs/screenshots/`**

```bash
cd "$WT"
mkdir -p docs/screenshots
for f in v1-results-desktop v1-gray-window v1-results-toolbar v1-results-rootmenu \
         v1-configmenu v1-slit-one-item v1-windowmenu v1-fullscreen; do
  cp "tests/golden/$f.png" "docs/screenshots/$f.png"
done
ls -la docs/screenshots/
```

Expected: 8 PNGs present. These are stable copies so a golden re-bless does not silently change README imagery.

- [ ] **Step 2: Re-verify the keybinding + config-knob facts (do not trust memory)**

```bash
cd "$WT"
sed -n '10,35p' src/Keybindings.cc        # the keybinding table
sed -n '57,97p'  src/Config.hh            # the rc knob list + defaults
rg -n 'setConfigOption|ConfigOption::' src/Server.cc | head   # live config-menu knobs (line drift after ext-workspace)
```

Expected: matches the tables in Task 1 / below. Correct any drift before writing.

- [ ] **Step 3: Write `README.md`**

Sections (SFW voice throughout - competent-reader, mechanism-first, honest about limits; no marketing adjectives):

1. **Title + one-liner.** `# BlackboxAI` / "The Blackbox window manager, reborn as a Wayland compositor." Lead image `docs/screenshots/v1-results-desktop.png`. State plainly: a from-scratch wlroots-0.20 C++20 compositor (`meson.build:3,16`), not an X11 port - it loads your original `.blackboxrc`, style, and menu files unchanged. One honesty line up top: pure-Wayland v1, XWayland deferred.

2. **Gallery** (the 8 copies, story order, one-line captions each): `v1-results-desktop` (the default Results look) -> `v1-results-toolbar` (toolbar, clock, workspace label) -> `v1-results-rootmenu` (the root menu) -> `v1-gray-window` (pixel-accurate SSD titlebar/handle/grips) -> `v1-windowmenu` (per-window ops) -> `v1-configmenu` (live re-theme/config UI) -> `v1-fullscreen` (window management) -> `v1-slit-one-item` (the SNI tray slit). One line setting expectations: these are headless golden renders (pixman, bundled mono font, fixed geometry), representative not glamour shots.

3. **"It's your Blackbox config."** Point `session.styleFile`/`menuFile` at your classic files. The 19 bundled styles (list them; default `Results`, chosen over Gray because Gray needs wildcard Xrm + Parent_Relative copy). Themed backgrounds are honored natively via `bsetroot` interpretation. Menu files load unchanged EXCEPT pipe menus (`[include] |cmd`, diagnosed not silent - `MenuParser.cc:203`). The rootCommand asymmetry: an rc `rootCommand` runs via `/bin/sh`, a style-file `rootCommand` never reaches a shell (deliberate - a theme should not run arbitrary shell).

4. **Keybindings** - the table from Task 1, generated from `src/Keybindings.cc`. Footnote: fixed in v1, not yet rc-configurable (`Keybindings.hh:4-5`'s "M5" comment never shipped - say so).

5. **Config knobs.** (a) The live Configuration menu (right-click desktop -> Configuration; `src/Server.cc` `setConfigOption`): focus model, focus-new-windows, auto/click-raise, placement (Row/Col-Smart/Center/Cascade), toolbar enable/placement/auto-hide, slit placement/direction/auto-hide - toggles persist to the rc via `updateRcKey` (`Config.hh:99-104`). (b) `.blackboxrc` keys for the rest (`Config.hh:57-97`): styleFile/menuFile/rootCommand, workspace count(4)/names, doubleClickInterval(250), mouse-wheel workspace/toolbar switching, `strftimeFormat` clock (live - `Toolbar.cc:75-76`). Default focus = SloppyFocus (focus-follows-mouse), default-on.

6. **Install** (short; deep-link `docs/install.md`). Two paths: COPR RPM (`sudo dnf copr enable hila-shemer/blackboxai && sudo dnf install blackboxai`, then pick BlackboxAI at GDM) or build from source. One line each.

7. **Honest limitations / non-goals** (the trust section, each with the WHY): no XWayland in v1 (pure Wayland; X11-only apps do not run); no window shade (xdg-shell has no shade concept - and `data/styles/Shade` is a THEME name, not the feature); tray context menus are text-only (no icon column in `bt::Menu`); `[restart]`/`[exit]` restart/kill the display server so every client goes with it (`main.cc:31-34`); `onTop` menu rows omitted (no lying toggle); keybindings not yet rc-configurable; workspace count is grow-only on live reconfigure; the locker is external (swaylock via `ext-session-lock-v1`). **Workspace-integration line: this may mention that BlackboxAI exports its workspaces over `ext-workspace-v1` for external panels/pagers - but ONLY because ext-workspace landed first this wave. Confirm the code is in (`rg ext_workspace_manager_v1 src/Server.cc`) before writing that sentence; otherwise describe only Super+arrow / toolbar switching.**

8. **Build/dev + license.** A "rewrite in the style of" bbidulock/blackboxwm, attributed; `bt::` gradient math ported verbatim. Link the design spec (`docs/superpowers/specs/`) and `man blackboxai`. MIT (`LICENSE`).

- [ ] **Step 4: VERIFY - links resolve, images exist, no stale/false claim**

```bash
cd "$WT"
echo "--- every referenced screenshot exists ---"
for p in $(grep -oE 'docs/screenshots/[A-Za-z0-9._-]+\.png' README.md | sort -u); do
  test -f "$p" && echo "OK $p" || { echo "MISSING $p"; exit 1; }
done
echo "--- every referenced doc link exists ---"
for p in $(grep -oE 'docs/[A-Za-z0-9._/-]+\.md' README.md | sort -u); do
  test -f "$p" && echo "OK $p" || echo "PENDING $p (Task 5 creates it)"
done
echo "--- the COPR owner string is the decided one ---"
grep -n 'copr enable hila-shemer/blackboxai' README.md
echo "--- no shade-as-feature, no rc-keybinding claim ---"
! grep -niE 'shade (window|state|feature)|rc-configurable keybinding|configurable via .blackboxrc.*key' README.md
echo "--- if ext-workspace is claimed, it must be in the tree ---"
if grep -qi 'ext-workspace' README.md; then rg -q 'ext_workspace_manager_v1' src/Server.cc && echo "ext-workspace claim OK" || { echo "FALSE ext-workspace claim"; exit 1; }; fi
```

Expected: all screenshots resolve; `docs/install.md`/`docs/gdm-session.md` are the only "PENDING" (Task 5); the COPR line uses `hila-shemer`; no shade-feature or rc-keybinding claim; any ext-workspace mention is backed by real code. Render the Markdown once (any viewer) to eye the gallery and tables.

- [ ] **Step 5: Commit**

```bash
git add README.md docs/screenshots/
git commit -m "Replace the README stub with the real product front page

The old README was two lines. This is what it is (drop-in Blackbox on Wayland,
loads your .blackboxrc/style/menu unchanged), an 8-shot gallery from the golden
set, the keybinding table generated from Keybindings.cc, the config knobs split
into the live Configuration menu and the rc keys, COPR install, and an honest
limitations section that names every non-goal with its reason - no XWayland, no
shade (xdg-shell has none; Shade is a theme name), text-only tray menus,
restart-kills-clients, keybindings fixed in v1. Screenshots are copied into
docs/screenshots/ so a golden re-bless never silently changes the imagery.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01DfRfEGgpiDzryN8MWZDQeb"
```

---

## Task 5: Install + GDM docs - `docs/install.md`, `docs/gdm-session.md`

**Files:**
- Create: `docs/install.md`
- Create: `docs/gdm-session.md`
- Read-only sources: `meson.build:15-92`, `.github/workflows/ci.yml:8-40`, `data/blackboxai.desktop`, `src/Server.cc:748-775` (autostart; line drift after ext-workspace), `docs/superpowers/plans/2026-07-03-productize-v1-program.md:19-21` (Ctrl+Alt+Backspace-suppressed-while-locked)

**Interfaces:**
- Consumes: the COPR id (Task 3), the container/Fedora build reality (`ci.yml`), the man page path (Task 1).
- Produces: the deep-link targets Task 4's README §6 points at.

- [ ] **Step 1: Re-verify the autostart + desktop facts**

```bash
cd "$WT"
cat data/blackboxai.desktop                                   # Name/Exec/DesktopNames
rg -n 'XDG_CURRENT_DESKTOP|shouldAutostart|/etc/xdg|.config/autostart' src/Server.cc
sed -n '8,40p' .github/workflows/ci.yml                       # the authoritative dep + build lines
```

Expected: `.desktop` has `Name=BlackboxAI`, `Exec=blackboxai`, `DesktopNames=Blackbox`; `Server.cc` sets `XDG_CURRENT_DESKTOP=Blackbox` and scans `~/.config/autostart` then `/etc/xdg/autostart`; `ci.yml` lists the deps. Correct any drift.

- [ ] **Step 2: Write `docs/install.md`**

Sections (SFW voice):

- **A. Build from source - two realities.** (1) The `blackboxai-ci:f44` container (fedora:44), the canonical path since this dev box ships no wlroots 0.20: the exact `docker run --rm --shm-size=1g ...` + `meson setup build && ninja -C build` lines, `--shm-size=1g` flagged as load-bearing for the test suite (default 64MB `/dev/shm` SIGBUS-kills screen-sized `wl_shm` buffers). (2) A Fedora host with `wlroots-devel` 0.20.1 (fc44+): native `meson setup build && ninja -C build && sudo ninja -C build install`. The full dep list from `ci.yml:14-17` (`wlroots-devel wayland-devel wayland-protocols-devel pixman-devel libdrm-devel libxkbcommon-devel libpng-devel fcft-devel systemd-devel` + `meson ninja-build gcc-c++ pkgconf sed`). **Explicit warning, lead with it:** Ubuntu/Debian ship only wlroots 0.19.x, so `meson setup` fails on the `>=0.20.0,<0.21.0` pin (`meson.build:16`) - use the container or a Fedora-44 host, do not `meson setup` on a stock Ubuntu box.
- **B. Install via COPR.** `sudo dnf copr enable hila-shemer/blackboxai` then `sudo dnf install blackboxai`. What the package drops: `/usr/bin/blackboxai`, `/usr/share/wayland-sessions/blackboxai.desktop`, `/usr/share/blackboxai/styles/`, `/usr/share/blackboxai/menu`, `man blackboxai`. fedora-44 only (the wlroots 0.20 pin resolves on no other chroot).
- **C. Pick it at GDM.** Short - the mechanics live in `gdm-session.md`; here just: log out, choose "BlackboxAI" at the greeter gear, log in. Link `gdm-session.md`.
- **D. Autostart.** Drop XDG `.desktop` files in `~/.config/autostart` or `/etc/xdg/autostart`; they run under `XDG_CURRENT_DESKTOP=Blackbox` (`Server.cc:750`). Target BlackboxAI specifically with `OnlyShowIn=Blackbox;`; `Hidden=true`/`NotShowIn=Blackbox;` suppress. Recommend `swaylock`/`swayidle` for lock + idle (protocol-only: `ext-session-lock-v1` + `ext-idle-notify-v1`; the compositor ships no locker).

- [ ] **Step 3: Write `docs/gdm-session.md`**

- **How the session is listed.** `data/blackboxai.desktop` installs to `/usr/share/wayland-sessions/blackboxai.desktop` (`meson.build:86-87`). `Name=BlackboxAI` is the label at the greeter; `Exec=blackboxai` (bare, resolved via `/usr/bin` on PATH); `DesktopNames=Blackbox` sets `XDG_CURRENT_DESKTOP=Blackbox` for the whole session. Three spellings, one thing - be explicit so a reader does not look for a `blackbox` binary.
- **The escape hatch.** `Ctrl+Alt+Backspace` = Quit the compositor (`Keybindings.cc`), the Wayland analogue of classic's X-server kill - but it is suppressed while a session lock is up (locked means locked; the wedged-locker escape is a kernel-side VT switch - `docs/superpowers/plans/2026-07-03-productize-v1-program.md:19-21`).
- **Troubleshooting.** Session missing at the greeter -> the `.desktop` was not installed / wrong `datadir` (check `/usr/share/wayland-sessions/`). Session bounces straight back to the greeter -> the compositor failed to init (check the journal; usually no seat / no DRM master). VT-switch survives a running session; multi-head is supported.

- [ ] **Step 4: VERIFY - facts trace to code, links resolve, no host-build trap**

```bash
cd "$WT"
echo "--- the build warning names the real pin ---"
grep -nE '0\.20|wlroots|Fedora 44|container' docs/install.md >/dev/null && echo OK
echo "--- COPR owner is the decided string ---"
grep -n 'copr enable hila-shemer/blackboxai' docs/install.md
echo "--- the session facts match the .desktop verbatim ---"
grep -q 'DesktopNames=Blackbox' data/blackboxai.desktop && grep -q 'XDG_CURRENT_DESKTOP' docs/gdm-session.md && echo OK
echo "--- README deep-links now resolve ---"
for p in docs/install.md docs/gdm-session.md; do test -f "$p" && echo "OK $p"; done
echo "--- no stock-Ubuntu meson-setup instruction ---"
! grep -niE 'ubuntu.*meson setup|apt install.*wlroots' docs/install.md
```

Expected: the pin/container warning is present; the COPR line uses `hila-shemer`; the GDM doc's `XDG_CURRENT_DESKTOP`/`DesktopNames` claims match the `.desktop`; both docs exist (Task 4's README links now resolve); no instruction tempts an Ubuntu reader into a host `meson setup` that fails on the 0.20 pin.

- [ ] **Step 5: Commit**

```bash
git add docs/install.md docs/gdm-session.md
git commit -m "Add install + GDM-session docs

Build-from-source with the two realities named up front: the blackboxai-ci:f44
container (this dev box ships no wlroots 0.20) or a Fedora-44 host - and a lead
warning that a stock Ubuntu meson setup fails on the >=0.20 pin, so nobody burns
an afternoon on it. COPR install, the GDM session mechanics (three spellings -
Name=BlackboxAI, Exec=blackboxai, DesktopNames=Blackbox - one thing), the
Ctrl+Alt+Backspace escape hatch and why it is dead while locked, and the
autostart contract (OnlyShowIn=Blackbox under XDG_CURRENT_DESKTOP=Blackbox).

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01DfRfEGgpiDzryN8MWZDQeb"
```

---

## Self-Review (run against the Wave-3 decisions before handing off)

**Spec coverage** (each Wave-3 decision -> its task):
- ext-workspace-v1: NOT this plan (lands first, own plan + review) - this plan's Task 4 only mentions it once, guarded by a live `rg` check.
- COPR owner hila-shemer: Task 3 (spec `URL`/comment) + Tasks 4/5 (`dnf copr enable` line). One string, checked in three VERIFY steps.
- fedora-44-only RPM: Task 3 (`%build` note + COPR comment) + Task 5 (§B).
- starter menu shipped: Task 2.
- man page as `configure_file` at `doc/blackboxai.1.in`: Task 1.
- prose docs under `docs/`, screenshots copied: Tasks 4-5.
- Keybindings.hh stale-comment honesty: Tasks 1/4 both document keybindings as fixed-in-v1 (the fix to the comment itself belongs to the ext-workspace review area per the program plan, since that is "the review area"; these docs must not paraphrase it).

**Placeholder scan:** no "TBD"/"handle edge cases"/"similar to Task N" - every meson block, spec, menu, and table is written out; porting-prose steps cite exact reference line ranges (the reference file is in-tree, so "port `:49-98` adapting X" is executable, not a placeholder).

**Consistency:** the keybinding table is identical in Task 1 and Task 4; the COPR id `hila-shemer/blackboxai` is identical across Tasks 3-5; `@pkgdatadir@` resolves to `/usr/share/blackboxai` in the man wiring (Task 1), the menu's literal `[stylesdir]` path (Task 2), and the RPM `%files` (Task 3) - all `<datadir>/blackboxai`.

**Load-order note carried:** every `Server.cc` citation in this plan is against the pre-ext-workspace tree (HEAD 9909f16) and each task's Step 1 re-verifies by symbol, because ext-workspace lands first and shifts those lines.
