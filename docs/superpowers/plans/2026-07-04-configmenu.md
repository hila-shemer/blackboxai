# Configmenu Implementation Plan (productize-v1, wave 2, lands 3rd)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The `[config]` placeholder becomes a live Configuration submenu - focus model / window placement / focus-new-windows rows that flip `config_` in memory and persist to the rc with classic spellings - plus the three debts parked on this slice: workspace-shrink re-home, the strftimeFormat clock swap, and the install-prefix watch-item fix.

**Architecture:** UI + persist only - the behaviors are window-mgmt's (it lands first and reads `config()` live). A pure builder (`rootmenu::buildConfigSubmenu`) produces checked/enabled rows off the Config; `fixupDynamic` mounts it per open (openRootMenu rebuilds the tree every open, so checkmark freshness is free, same as the workspaces submenu); one new `Act::ConfigOption` dispatches to `Server::setConfigOption`, which is classic toggle=set+save semantics - mutate `config_`, `applyConfig()`, `updateRcKey` - deliberately NOT `reconfigure()` (that would reload the style, re-run the rc rootCommand, and revert unpersisted in-memory state on every toggle).

**Tech Stack:** C++20, meson+ninja, doctest, headless golden-PNG harness (`tests/harness/`). Zero new wlroots or sd-bus surface - menus are existing scene chrome, persist is `updateRcKey`, the clock swap is a libc format string already wrapped by `bt::formatClock`. No container header re-cite needed (scout POC compiled the internal seams green in `blackboxai-ci:f44`).

## Global Constraints

- wlroots pinned 0.20 (`meson.build`); wlroots reaches code ONLY via `toolkit/wlr.hpp`. This slice adds no wlroots calls - keep it that way.
- Coverage gate: `gcovr -r . build-f44 --filter toolkit/ --filter src/ --fail-under-line=80`; project actual is ~91% - do not spend the slack. Every new Server method here is headlessly reachable by construction (no fork paths - headless Servers default to a recording `FakeCommandRunner`, `Server.cc:280`).
- **Golden policy for this slice:** ONE new golden (`v1-configmenu.png`, Task 8) plus ONE declared re-bless: `m5-menufile.png` + `m5-menufile-cascade.png` flip in Task 2 because the mount renders the Configuration row enabled instead of grey - a semantic change this slice explicitly ships, not drift (rc-style's "one golden flipped by design" precedent, train-log stop 4). Every other golden stays byte-identical; the final gate proves it. The menus slice may later sweep our new golden in its coordinated margin re-bless - sanctioned, theirs.
- Any test that renders text or computes click coordinates registers with `text_env` (fontconfig isolation, `tests/meson.build:84`) and guards `REQUIRE(server.titleFont()->height() == 18)` - gotcha #20.
- Click coordinates derive from the menu's own accessors (`itemIndexAtGlobal` scan + `rectX/YForTest`), never baked margin math - synthesis obligation, so menus' possible margin swap re-blesses pixels without rewriting our tests.
- Each task ends green through the container gate (this box has no wlroots - host `meson setup` fails, don't chase it):

```sh
WT=<absolute worktree path>   # worktrees mount /home/hila/proj so .git links stay valid
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" blackboxai-ci:f44 bash -c '
  [ -d build-f44 ] || meson setup build-f44 -Db_coverage=true -Dbuildtype=debug
  ninja -C build-f44 &&
  export XDG_RUNTIME_DIR=$(mktemp -d) && chmod 700 "$XDG_RUNTIME_DIR" &&
  WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-f44 -j 4'
```

- Scratch/POCs: `/tmp/claude-1000/-home-hila-proj-blackboxai/8b5e93a4-d1a9-4374-9185-6a529349f965/scratchpad/planning-w2-configmenu/` - never into the repo.
- **Not ours (wave-2 territory law):** `src/Config.{hh,cc}` parse layer (window-mgmt is the sole Config editor this wave - we consume `config()` read-only and write rc keys only via `updateRcKey`), `src/Menu.{hh,cc}` + `Menu.geom.hh` (menus), `src/View.*` + `src/Keybindings.*` (window-mgmt), `src/Style.*` + `src/SniHost.*` (slit/menus). One declared exception at the merge slot, below.

## Merge-train position and seam discipline

We develop in parallel from the wave-1 tip (`8a3fc49`) and land THIRD (window-mgmt → slit → **configmenu** → menus). Consequences:

- **Zero stubs.** Everything we consume (`applyConfig`, `updateRcKey`, `config()`, the `Act::ConfigMenu` placeholder, Menu rendering incl. drawCheck/drawArrow/disabled) landed in wave 1. We take nothing from window-mgmt or slit.
- **We PROVIDE (seam contracts, law):** `Act::ConfigOption` appended immediately after `ConfigMenu` + `enum class ConfigOption` (namespace-level in `MenuItem.hh`, our 9 values in the pinned order) + the `option` payload field; `void Server::setConfigOption(ConfigOption opt)`; `rootmenu::buildConfigSubmenu(const Config &cfg)` exported for unit tests; `buildFromParsed` gaining the `Config&` param. menus appends its `ConfigOption` values and `Act` values AFTER ours, tail-only, and extends `setConfigOption`'s switch - two slices, one dispatch idiom.
- **One pinned-signature deviation, declared:** the synthesis spells `buildFromParsed(..., WorkspaceModel &ws, ...)`; the landed function takes `const WorkspaceModel &` and no wave-2 sibling calls it (verified: callers are `Server.cc:1397` + `tests/unit/rootmenu_wire_test.cc` only). We keep the `const` - dropping it buys nothing and no consumer exists to care.
- **Expected conflicts at our slot** (window-mgmt + slit already landed): `Server.cc` member/ctor regions and `tests/meson.build` appends - mechanical. We do NOT edit `applyConfig`'s body (rc workspace path stays grow-only, locked), so slit's applyConfig slit-knob lines merge clean. Our `handleMenuButton` two-line guard and `activateMenuItem` tail case sit in menus' modal region - we land first, menus rebases over us.
- **Merge-slot checklist** (train commit, out-of-marker jobs allowed by stop-4 precedent): (1) assert window-mgmt's locked default landed - `Config{}.focusModel == FocusModel::SloppyFocus` - which makes the Sloppy row default-checked with a key-less rc; our tests set `focusModel` explicitly so they hold on both sides of that flip. (2) Update the now-stale `Config.hh:84` comment (`strftimeFormat` "parse-only this wave" - Task 6 makes it live); comment-only touch of window-mgmt's file, done at the slot to avoid a parallel-worktree conflict. (3) Full gate + goldens clean.

## Locked decisions + critique responses binding this plan

- **Behaviors live in window-mgmt (locked):** sloppy focus + AutoRaise/ClickRaise + placement algorithms are window-mgmt's; we ship rows that mutate/persist config and it reads config live. The scout's "implement minimal sloppy focus here" recommendation is dead - adopted per the user decision.
- **Sloppy focus DEFAULT-ON (locked, user law):** the Config default flip is window-mgmt's (`Config.*` owner). Our rows read `cfg.focusModel`, so default-checked follows automatically; verified at the merge slot (checklist above), and our own tests never assert a default-constructed Config's focus model so they are stable across the flip.
- **Toolbar Options / Slit Options rows OMITTED** (seam contract): those menus open via right-click gestures (menus slice). No disabled placeholders, no lying rows. **Image Dithering omitted** (no engine). **onTop rows omitted** everywhere (program law; parsed keys stay inert). **Opaque Move/Resize, Full Maximization, Focus Last Window, wheel toggles, Disable Bindings omitted**: no parsed key or no consumer - a row without behavior is a lying toggle. The Configuration submenu is exactly: Focus Model, Window Placement, separator, Focus New Windows. Placement direction rows (LeftRight/TopBottom...) and IgnoreShadedWindows are also out - no direction machinery, no shade (accepted parity gaps).
- **In-code fallback menu does NOT gain a Configuration row** (rebutting the scout sketch): it would change `rootmenu::build`'s signature, churn `m4-rootmenu*.png` + the styled-menu golden, and break `menu_action_test`'s pinned item indexes (5=Restart, 6=Exit) - real churn against the program's zero-churn law for our slot, for a row every menu-file user gets via `[config]` anyway. The fallback stays a broken-boot lifeboat.
- **Workspace shrink stays grow-only on the rc path** (plan-author call, locked): shrink + re-home ONLY via the explicit RemoveWorkspace action (Task 5). `applyConfig` untouched.
- **strftimeFormat swap is render-only** (locked wave-1 deferral now due): one line in `Toolbar::clockText`; the compiled default format string is identical, so existing clock goldens hold - the sanctioned re-bless is unnecessary; a custom-format fixture test lands instead (Task 6).
- **Watch-item ownership** (synthesis): the install-prefix pattern is at `retheme_test.cc:138` AND `style_boot_test.cc:78` (the program plan's ":99" drifted). One mechanism fixes both (Task 7).
- **Ripple correction adopted:** `buildFromParsed` callers are exactly `src/Server.cc:1397` + `tests/unit/rootmenu_wire_test.cc` (5 uses). The synthesis missed one *behavioral* ripple, found at plan time: `tests/system/menu_file_test.cc:223-244` pins the placeholder as disabled and its goldens render the grey row - adapted + re-blessed in Task 2 (mechanism changed, requirement kept: the row must render, now live).
- **Per-screen-key shadowing, noted not coded:** we persist the global `session.focusModel`; a user rc that only sets `session.screen0.focusModel` gets shadowed by our written global. Classic wrote the global too - acceptable, no diagnostic.

## File map

| File | Change |
|---|---|
| `src/MenuItem.hh` | `Act::ConfigOption` + `enum class ConfigOption` (9 values, pinned order) + `option` field - append-only |
| `src/Rootmenu.hh` / `.cc` | `buildConfigSubmenu(const Config&)`; `buildFromParsed`/`fixupDynamic` gain `const Config&`; mount |
| `src/ConfigSpelling.hh` | NEW, header-only: classic rc value spellings (writer side; Config.cc is not ours) |
| `src/Server.hh` / `.cc` | `setConfigOption` + dispatch case; disabled-row guard in `handleMenuButton`; `removeLastWorkspaceAndRehome` + RemoveWorkspace rewire; `openRootMenu` call site; `loadStyleWithFallback` headless default-hiding + `default_style_path_` + `setDefaultStyleForTest` |
| `src/Toolbar.cc` | `clockText` passes `config().strftimeFormat` (one line) |
| `src/MenuParser.cc` | drop the now-false "[config] shown disabled" diagnostic (3 lines) |
| `tests/unit/configmenu_test.cc` | NEW: builder matrix + spelling round-trips |
| `tests/unit/rootmenu_wire_test.cc` | 5 call sites gain `cfg`; disabled-placeholder test flips to mounted |
| `tests/unit/menu_parser_test.cc` | drop the config-diagnostic CHECK |
| `tests/system/configmenu_test.cc` | NEW: click-through toggle/persist/reopen + golden (Task 8) |
| `tests/system/workspace_remove_test.cc` | NEW: shrink re-home + focus repair |
| `tests/system/clock_format_test.cc` | NEW: strftimeFormat fixture |
| `tests/system/menu_file_test.cc` | placeholder pins adapt to the mount; 2 goldens re-blessed |
| `tests/system/retheme_test.cc` / `style_boot_test.cc` | watch-item comments + middle-rung test |
| `tests/meson.build` | register the 4 new test files |
| `tests/golden/` | +`v1-configmenu.png`; `m5-menufile{,-cascade}.png` re-blessed |

---

### Task 1: ConfigOption payload + pure Configuration-submenu builder

**Files:**
- Modify: `src/MenuItem.hh` (Act tail at line 22, new enum + field)
- Modify: `src/Rootmenu.hh:19` (export), `src/Rootmenu.cc` (builder + include)
- Create: `tests/unit/configmenu_test.cc`
- Modify: `tests/meson.build:35` (unit_sources list)

**Interfaces:**
- Consumes: `bbai::Config` fields `focusModel/autoRaise/clickRaise/focusNewWindows/windowPlacement` (`src/Config.hh:66-75`, read-only), `MenuItem` (`checked`/`enabled` already rendered by Menu.cc drawCheck/disabled - zero rendering work).
- Produces: `enum class bbai::ConfigOption { FocusClickToFocus, FocusSloppy, AutoRaise, ClickRaise, FocusNewWindows, PlacementRowSmart, PlacementColSmart, PlacementCenter, PlacementCascade }` (namespace-level in MenuItem.hh, THIS order - menus appends after `PlacementCascade`); `MenuItem::Act::ConfigOption` (immediately after `ConfigMenu`); `MenuItem::option` field; `std::vector<MenuItem> rootmenu::buildConfigSubmenu(const Config &cfg)`. Tasks 2 and 4 consume all of these under these exact names.

- [x] **Step 1: Write the failing test**

Create `tests/unit/configmenu_test.cc`. Do NOT assert a default-constructed Config's focus model anywhere - window-mgmt flips the default to SloppyFocus (locked) and lands before us; explicit fields keep this file green on both sides of the merge.

```cpp
// Pure Configuration-submenu build: the checked/enabled matrix mirrors classic
// Configmenu::refresh (reference/blackboxwm/src/Configmenu.cc:242-253,319-347).
// No wlroots, no rendering. Config fields are always set EXPLICITLY here -
// window-mgmt owns (and flips) the focus-model default, and this file must be
// merge-stable across that.
#include <doctest/doctest.h>
#include "Rootmenu.hh"
#include "Config.hh"
#include "Text.hh"

using namespace bbai;

TEST_CASE("shape: Focus Model + Window Placement submenus, separator, Focus New Windows") {
  Config cfg;
  auto items = rootmenu::buildConfigSubmenu(cfg);
  REQUIRE(items.size() == 4);
  CHECK(items[0].kind == MenuItem::Kind::Submenu);
  CHECK(items[0].label == bt::decodeUtf8("Focus Model"));
  REQUIRE(items[0].submenu_items.size() == 4);
  CHECK(items[1].kind == MenuItem::Kind::Submenu);
  CHECK(items[1].label == bt::decodeUtf8("Window Placement"));
  REQUIRE(items[1].submenu_items.size() == 4);
  CHECK(items[2].separator());
  CHECK(items[3].action == MenuItem::Act::ConfigOption);
  CHECK(items[3].option == ConfigOption::FocusNewWindows);
  CHECK(items[3].label == bt::decodeUtf8("Focus New Windows"));
}

TEST_CASE("ClickToFocus: CTF radio checked, raise rows disabled (classic sloppy-only enabling)") {
  Config cfg;
  cfg.focusModel = FocusModel::ClickToFocus;
  cfg.autoRaise = false;
  cfg.clickRaise = false;
  auto fm = rootmenu::buildConfigSubmenu(cfg)[0].submenu_items;
  CHECK(fm[0].checked);
  CHECK(fm[0].enabled);
  CHECK(fm[0].option == ConfigOption::FocusClickToFocus);
  CHECK_FALSE(fm[1].checked);
  CHECK(fm[1].option == ConfigOption::FocusSloppy);
  CHECK_FALSE(fm[2].enabled);
  CHECK_FALSE(fm[2].checked);
  CHECK_FALSE(fm[3].enabled);
  CHECK_FALSE(fm[3].checked);
}

TEST_CASE("SloppyFocus + AutoRaise: radios exclusive, raise rows enabled and checked from cfg") {
  Config cfg;
  cfg.focusModel = FocusModel::SloppyFocus;
  cfg.autoRaise = true;
  cfg.clickRaise = false;
  auto fm = rootmenu::buildConfigSubmenu(cfg)[0].submenu_items;
  CHECK_FALSE(fm[0].checked);
  CHECK(fm[1].checked);
  CHECK(fm[2].enabled);
  CHECK(fm[2].checked);
  CHECK(fm[2].option == ConfigOption::AutoRaise);
  CHECK(fm[3].enabled);
  CHECK_FALSE(fm[3].checked);
  CHECK(fm[3].option == ConfigOption::ClickRaise);
}

TEST_CASE("placement radios: exactly the configured policy is checked, all rows enabled") {
  Config cfg;
  cfg.windowPlacement = WindowPlacement::Cascade;
  auto wp = rootmenu::buildConfigSubmenu(cfg)[1].submenu_items;
  CHECK_FALSE(wp[0].checked);
  CHECK_FALSE(wp[1].checked);
  CHECK_FALSE(wp[2].checked);
  CHECK(wp[3].checked);
  for (const MenuItem &m : wp) CHECK(m.enabled);
  CHECK(wp[0].option == ConfigOption::PlacementRowSmart);
  CHECK(wp[1].option == ConfigOption::PlacementColSmart);
  CHECK(wp[2].option == ConfigOption::PlacementCenter);
  CHECK(wp[3].option == ConfigOption::PlacementCascade);
}

TEST_CASE("Focus New Windows mirrors cfg both ways") {
  Config cfg;
  cfg.focusNewWindows = false;
  CHECK_FALSE(rootmenu::buildConfigSubmenu(cfg)[3].checked);
  cfg.focusNewWindows = true;
  CHECK(rootmenu::buildConfigSubmenu(cfg)[3].checked);
}
```

Register it - in `tests/meson.build`, add to `unit_sources` after `'unit/rootmenu_wire_test.cc',`:

```meson
  'unit/configmenu_test.cc',
```

- [x] **Step 2: Run the test to verify it fails**

```sh
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" blackboxai-ci:f44 bash -c '
  [ -d build-f44 ] || meson setup build-f44 -Db_coverage=true -Dbuildtype=debug
  ninja -C build-f44'
```

Expected: compile FAILURE - `buildConfigSubmenu` is not a member of `bbai::rootmenu`, `ConfigOption` not declared.

- [x] **Step 3: Implement**

`src/MenuItem.hh` - append `ConfigOption` to `Act` (after `ConfigMenu`, line 22) and add the namespace-level enum + field:

```cpp
    enum class Act {
      None, Exec, WorkspaceSwitch, NewWorkspace, RemoveWorkspace, Restart, Exit,
      Deiconify,
      // M5 menu-file actions:
      SetStyle,        // argv[0] = tilde-expanded style file path
      Reconfigure,     // classic [reconfig]; a documented {cmd} is dropped + noted
      RestartOther,    // argv = {"/bin/sh", "-c", "exec " + cmd}
      WorkspacesMenu,  // marker on the [workspaces] placeholder Submenu
      ConfigMenu,      // marker on the [config] placeholder Submenu (wave-2 mount)
      ConfigOption     // Configuration-submenu row -> Server::setConfigOption
    };
```

Before `struct MenuItem` (still inside `namespace bbai`):

```cpp
  // Configuration-menu knobs (wave-2 seam contract). Radio values encode the
  // target state; toggle values name the knob to flip. APPEND-ONLY at the
  // tail - the menus slice adds its Toolbar*/Slit* values after ours, and
  // Server::setConfigOption's switch grows in the same per-slice groups.
  enum class ConfigOption {
    FocusClickToFocus, FocusSloppy, AutoRaise, ClickRaise, FocusNewWindows,
    PlacementRowSmart, PlacementColSmart, PlacementCenter, PlacementCascade
  };
```

Inside `struct MenuItem`, after the `target` field:

```cpp
    ConfigOption option = ConfigOption::FocusClickToFocus;  // valid iff action == Act::ConfigOption
```

`src/Rootmenu.hh` - after the `buildWorkspacesSubmenu` declaration:

```cpp
  // The Configuration submenu, checked/enabled off the live Config (classic
  // Configmenu::refresh semantics). Only rows with real behavior behind them:
  // Focus Model, Window Placement, Focus New Windows. Toolbar/Slit Options,
  // dithering, onTop and the rest of classic's toggle pile are omitted -
  // no lying rows (program law; deviations documented in the wave-2 plan).
  std::vector<MenuItem> buildConfigSubmenu(const Config &cfg);
```

Add `#include "Config.hh"` to `src/Rootmenu.hh` (below `#include "Workspace.hh"`).

`src/Rootmenu.cc` - add after `buildWorkspacesSubmenu` (reuse the existing anonymous-namespace `separator()` helper; add `option()` next to it):

```cpp
    MenuItem option(const char *label, ConfigOption opt, bool checked, bool enabled) {
      MenuItem m;
      m.label = bt::decodeUtf8(label);
      m.action = MenuItem::Act::ConfigOption;
      m.option = opt;
      m.checked = checked;
      m.enabled = enabled;
      return m;
    }
```

(`option()` goes inside the same `namespace { ... }` block as `exec`/`separator`/`simple` at the top of the file.)

```cpp
  std::vector<MenuItem> buildConfigSubmenu(const Config &cfg) {
    std::vector<MenuItem> items;

    // Classic ConfigFocusmenu: CTF/Sloppy are radios; AutoRaise/ClickRaise
    // only mean anything under sloppy, so they are disabled otherwise.
    MenuItem focus;
    focus.kind = MenuItem::Kind::Submenu;
    focus.label = bt::decodeUtf8("Focus Model");
    const bool sloppy = (cfg.focusModel == FocusModel::SloppyFocus);
    focus.submenu_items.push_back(
        option("Click to Focus", ConfigOption::FocusClickToFocus, !sloppy, true));
    focus.submenu_items.push_back(
        option("Sloppy Focus", ConfigOption::FocusSloppy, sloppy, true));
    focus.submenu_items.push_back(
        option("Auto Raise", ConfigOption::AutoRaise, cfg.autoRaise, sloppy));
    focus.submenu_items.push_back(
        option("Click Raise", ConfigOption::ClickRaise, cfg.clickRaise, sloppy));
    items.push_back(std::move(focus));

    // Classic ConfigPlacementmenu, radios only - the direction rows need
    // machinery we don't have (documented omission).
    MenuItem place;
    place.kind = MenuItem::Kind::Submenu;
    place.label = bt::decodeUtf8("Window Placement");
    const WindowPlacement wp = cfg.windowPlacement;
    place.submenu_items.push_back(option("Smart Placement (Rows)",
        ConfigOption::PlacementRowSmart, wp == WindowPlacement::RowSmart, true));
    place.submenu_items.push_back(option("Smart Placement (Columns)",
        ConfigOption::PlacementColSmart, wp == WindowPlacement::ColSmart, true));
    place.submenu_items.push_back(option("Center Placement",
        ConfigOption::PlacementCenter, wp == WindowPlacement::Center, true));
    place.submenu_items.push_back(option("Cascade Placement",
        ConfigOption::PlacementCascade, wp == WindowPlacement::Cascade, true));
    items.push_back(std::move(place));

    items.push_back(separator());
    items.push_back(option("Focus New Windows", ConfigOption::FocusNewWindows,
                           cfg.focusNewWindows, true));
    return items;
  }
```

- [x] **Step 4: Run the test to verify it passes**

```sh
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" blackboxai-ci:f44 bash -c '
  ninja -C build-f44 &&
  export XDG_RUNTIME_DIR=$(mktemp -d) && chmod 700 "$XDG_RUNTIME_DIR" &&
  WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-f44 -j 4'
```

Expected: all suites pass (the new unit cases run inside the `unit` test).

- [x] **Step 5: Commit**

```sh
git add src/MenuItem.hh src/Rootmenu.hh src/Rootmenu.cc tests/unit/configmenu_test.cc tests/meson.build
git commit -m "Configmenu: pure Configuration-submenu builder + the ConfigOption payload

One Act (ConfigOption) + one option enum instead of per-knob Acts - menus
reuses the idiom and appends its Toolbar*/Slit* values at the tail, so the
shared enum merges by construction. Rows mirror classic refresh(): radios
carry target state, AutoRaise/ClickRaise are sloppy-only. Only honest rows -
Toolbar/Slit Options, dithering, onTop et al are documented omissions."
```

---

### Task 2: Mount - buildFromParsed carries the Config

**Files:**
- Modify: `src/Rootmenu.hh:26-27`, `src/Rootmenu.cc:59-81` (fixupDynamic + buildFromParsed)
- Modify: `src/Server.cc:1397` (openRootMenu call site)
- Modify: `src/MenuParser.cc:163-180` (drop the stale diagnostic)
- Test: `tests/unit/rootmenu_wire_test.cc`, `tests/unit/menu_parser_test.cc:177-180`, `tests/system/menu_file_test.cc:223-244`
- Re-bless: `tests/golden/m5-menufile.png`, `tests/golden/m5-menufile-cascade.png`

**Interfaces:**
- Consumes: `rootmenu::buildConfigSubmenu` (Task 1), `Server::config_` (the member - we are inside Server.cc).
- Produces: `std::vector<MenuItem> rootmenu::buildFromParsed(const std::vector<MenuItem> &parsed, const WorkspaceModel &ws, const Config &cfg)` - the wave-2 pinned signature (const-preserving, see header). Task 4's system tests rely on the mount being live.

- [x] **Step 1: Write the failing tests**

In `tests/unit/rootmenu_wire_test.cc`: add `#include "Config.hh"`, then replace the disabled-placeholder test (lines 48-57) with:

```cpp
TEST_CASE("a ConfigMenu placeholder mounts the live Configuration submenu") {
  WorkspaceModel ws;
  Config cfg;
  cfg.focusNewWindows = false;   // prove the rows read THIS config
  std::vector<MenuItem> parsed{marked(MenuItem::Act::ConfigMenu, "Configuration")};

  auto items = rootmenu::buildFromParsed(parsed, ws, cfg);
  REQUIRE(items.size() == 1);
  CHECK(items[0].enabled);
  CHECK(items[0].selectable());
  REQUIRE(items[0].submenu_items.size() == 4);
  CHECK(items[0].submenu_items[0].label == u("Focus Model"));
  CHECK_FALSE(items[0].submenu_items[3].checked);   // focusNewWindows=false above
}
```

Update the other four `buildFromParsed` calls in the file (lines 27, 42, 61, 74) to pass a Config - add `Config cfg;` beside each `WorkspaceModel ws;` and append the argument, e.g. line 27:

```cpp
  auto items = rootmenu::buildFromParsed(parsed, ws, cfg);
```

(same one-line change at 42, 61 - `rootmenu::buildFromParsed({}, ws, cfg)` - and 74 - `rootmenu::buildFromParsed({e}, ws, cfg)`).

In `tests/unit/menu_parser_test.cc:177-180`, the mount makes the parser's config note false - replace:

```cpp
  // [workspaces] is fully handled from here on - no diagnostic. [config]
  // keeps a note until wave-2 configmenu populates it.
  CHECK_FALSE(anyDiagContains(r, "workspaces"));
  CHECK(anyDiagContains(r, "config"));
```

with:

```cpp
  // Both placeholders are fully handled at open time - no diagnostics.
  CHECK(r.diagnostics.empty());
```

In `tests/system/menu_file_test.cc`: line 224 becomes

```cpp
  CHECK(m->item(3).enabled);                                    // mounted live (wave-2)
```

and the tail check (lines 240-244) flips from "must NOT hover-open" to the live cascade:

```cpp
  // The Configuration row now hover-opens the live config cascade (wave-2 mount).
  const int cfg_y = oy + menu::titleHeight(18) + menu::kFrameMargin
                  + 3 * row_h + row_h / 2;
  server.injectPointerMotionForTest(ox + 30, cfg_y);
  REQUIRE(m->submenuOpenForTest());
  CHECK(m->submenuItemCountForTest() == 4);   // Focus Model / Placement / sep / Focus New Windows
```

- [x] **Step 2: Run to verify failure**

```sh
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" blackboxai-ci:f44 bash -c 'ninja -C build-f44'
```

Expected: compile FAILURE - `buildFromParsed` takes 2 arguments, 3 given.

- [x] **Step 3: Implement**

`src/Rootmenu.hh:20-27` - replace the buildFromParsed doc + declaration:

```cpp
  // Live fixup of a parsed menu-file tree: [workspaces] placeholders get the
  // real workspace submenu and [config] placeholders the Configuration
  // submenu (both rebuilt per open, so current-marks and checkmarks stay
  // honest), and an empty parse falls back to the in-code menu - a broken
  // first boot still gets a terminal + workspaces, not classic's bare xterm
  // stub. The fallback deliberately has no [config] row: adding one would
  // churn the m4 menu goldens + pinned item indexes for a lifeboat menu.
  std::vector<MenuItem> buildFromParsed(const std::vector<MenuItem> &parsed,
                                        const WorkspaceModel &ws,
                                        const Config &cfg);
```

`src/Rootmenu.cc:59-81` - thread the Config through:

```cpp
  namespace {
    void fixupDynamic(std::vector<MenuItem> &items, const WorkspaceModel &ws,
                      const Config &cfg) {
      for (MenuItem &m : items) {
        if (m.kind != MenuItem::Kind::Submenu) continue;
        if (m.action == MenuItem::Act::WorkspacesMenu) {
          m.submenu_items = buildWorkspacesSubmenu(ws);
        } else if (m.action == MenuItem::Act::ConfigMenu) {
          m.enabled = true;                        // wave-2 mount: live rows
          m.submenu_items = buildConfigSubmenu(cfg);
        } else {
          fixupDynamic(m.submenu_items, ws, cfg);
        }
      }
    }
  } // namespace

  std::vector<MenuItem> buildFromParsed(const std::vector<MenuItem> &parsed,
                                        const WorkspaceModel &ws,
                                        const Config &cfg) {
    if (parsed.empty()) return build(ws);   // locked: fall back to the RICHER in-code menu
    std::vector<MenuItem> items = parsed;
    fixupDynamic(items, ws, cfg);
    return items;
  }
```

`src/Server.cc:1397`:

```cpp
        : rootmenu::buildFromParsed(menu_items_, workspaces_, config_);
```

`src/MenuParser.cc` - in the `workspaces/config` branch, update the comment and drop the note (lines 164-168 and 178-180):

```cpp
        } else if (tag == "workspaces" || tag == "config") {
          // Runtime-populated menus: the parser can't enumerate workspaces or
          // config options, so it emits an empty placeholder Submenu carrying
          // an Act marker (dead weight on Kind::Submenu - zero layout impact)
          // that the wire-up resolves at open time.
          if (label.empty()) {
            diag.push_back(note(lineNo, "[" + tag + "] needs a label - skipped"));
            continue;
          }
          MenuItem m;
          m.kind = MenuItem::Kind::Submenu;
          m.label = bt::decodeUtf8(label.c_str());
          m.action = (tag == "workspaces") ? MenuItem::Act::WorkspacesMenu
                                           : MenuItem::Act::ConfigMenu;
          out.push_back(std::move(m));
        } else if (tag == "submenu") {
```

- [x] **Step 4: Re-bless the two mount-affected goldens, then run the full gate**

The Configuration row now renders enabled (normal text, not `frameDisabled` grey) in both m5-menufile captures - the declared by-design flip:

```sh
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" blackboxai-ci:f44 bash -c '
  ninja -C build-f44 &&
  export XDG_RUNTIME_DIR=$(mktemp -d) && chmod 700 "$XDG_RUNTIME_DIR" &&
  BLESS=1 WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-f44 menu_file &&
  WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-f44 -j 4'
```

Expected: all pass. Then `git status tests/golden/` must show EXACTLY `m5-menufile.png` and `m5-menufile-cascade.png` modified - anything else is a regression, stop and investigate.

- [x] **Step 5: Commit**

```sh
git add src/Rootmenu.hh src/Rootmenu.cc src/Server.cc src/MenuParser.cc \
        tests/unit/rootmenu_wire_test.cc tests/unit/menu_parser_test.cc \
        tests/system/menu_file_test.cc tests/golden/m5-menufile.png \
        tests/golden/m5-menufile-cascade.png
git commit -m "Rootmenu: mount the Configuration submenu on [config]

buildFromParsed carries the Config; openRootMenu rebuilds per open so
checkmark freshness is free (workspaces-submenu precedent). The two
m5-menufile goldens re-bless by design - the row renders live instead of
disabled grey; that flip IS the feature. Parser's 'shown disabled' note
dropped with it. In-code fallback stays config-less (golden + index churn
for a lifeboat menu - not worth it)."
```

---

### Task 3: Classic persist spellings (pure)

**Files:**
- Create: `src/ConfigSpelling.hh` (header-only)
- Test: `tests/unit/configmenu_test.cc` (append)

**Interfaces:**
- Consumes: `bbai::Config`, `bbai::updateRcKey` (`src/Config.hh:94`) and `Config::load` in tests only.
- Produces: `std::string bbai::configmenu::focusModelValue(const Config &cfg)`; `const char *bbai::configmenu::windowPlacementValue(WindowPlacement p)`. Task 4's `setConfigOption` consumes both.

- [x] **Step 1: Write the failing test**

Append to `tests/unit/configmenu_test.cc` (add `#include "ConfigSpelling.hh"` and `#include <cstdio>` at the top):

```cpp
TEST_CASE("persist spellings: composite focusModel round-trips through the parser") {
  Config cfg;
  cfg.focusModel = FocusModel::SloppyFocus;
  cfg.autoRaise = true;
  cfg.clickRaise = true;
  CHECK(configmenu::focusModelValue(cfg) == "SloppyFocus AutoRaise ClickRaise");

  cfg.clickRaise = false;
  CHECK(configmenu::focusModelValue(cfg) == "SloppyFocus AutoRaise");

  const char *p = "/tmp/bbai-configmenu-spelling.rc";
  std::remove(p);
  cfg.clickRaise = true;
  REQUIRE(bbai::updateRcKey(p, "session.focusModel", configmenu::focusModelValue(cfg)));
  Config re = Config::load(p);
  CHECK(re.focusModel == FocusModel::SloppyFocus);
  CHECK(re.autoRaise);
  CHECK(re.clickRaise);

  cfg.focusModel = FocusModel::ClickToFocus;
  CHECK(configmenu::focusModelValue(cfg) == "ClickToFocus");
  REQUIRE(bbai::updateRcKey(p, "session.focusModel", configmenu::focusModelValue(cfg)));
  re = Config::load(p);
  CHECK(re.focusModel == FocusModel::ClickToFocus);
  CHECK_FALSE(re.autoRaise);   // the parser forces both raise flags off under CTF
  CHECK_FALSE(re.clickRaise);
  std::remove(p);
}

TEST_CASE("persist spellings: windowPlacement strings round-trip") {
  using WP = WindowPlacement;
  CHECK(std::string(configmenu::windowPlacementValue(WP::RowSmart)) == "RowSmartPlacement");
  CHECK(std::string(configmenu::windowPlacementValue(WP::ColSmart)) == "ColSmartPlacement");
  CHECK(std::string(configmenu::windowPlacementValue(WP::Center)) == "CenterPlacement");
  CHECK(std::string(configmenu::windowPlacementValue(WP::Cascade)) == "CascadePlacement");

  const char *p = "/tmp/bbai-configmenu-spelling.rc";
  std::remove(p);
  REQUIRE(bbai::updateRcKey(p, "session.windowPlacement",
                            configmenu::windowPlacementValue(WP::Cascade)));
  CHECK(Config::load(p).windowPlacement == WP::Cascade);
  std::remove(p);
}
```

- [x] **Step 2: Run to verify failure**

```sh
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" blackboxai-ci:f44 bash -c 'ninja -C build-f44'
```

Expected: compile FAILURE - `ConfigSpelling.hh: No such file or directory`.

- [x] **Step 3: Implement**

Create `src/ConfigSpelling.hh`:

```cpp
// Classic rc value spellings for the Configuration-menu persist path -
// BlackboxResource.cc:266-296 save() composes exactly these strings, and our
// Config parser (substring find for the focus composite, iequals for
// placement) reads them back. Header-only and pure on purpose: Config.cc is
// window-mgmt territory this wave, so the WRITER side of the spellings lives
// here and configmenu persists through updateRcKey only.
#ifndef BLACKBOXAI_CONFIGSPELLING_HH
#define BLACKBOXAI_CONFIGSPELLING_HH

#include "Config.hh"

#include <string>

namespace bbai::configmenu {

  inline std::string focusModelValue(const Config &cfg) {
    if (cfg.focusModel == FocusModel::ClickToFocus) return "ClickToFocus";
    std::string s = "SloppyFocus";
    if (cfg.autoRaise) s += " AutoRaise";
    if (cfg.clickRaise) s += " ClickRaise";
    return s;
  }

  inline const char *windowPlacementValue(WindowPlacement p) {
    switch (p) {
    case WindowPlacement::ColSmart: return "ColSmartPlacement";
    case WindowPlacement::Center:   return "CenterPlacement";
    case WindowPlacement::Cascade:  return "CascadePlacement";
    case WindowPlacement::RowSmart: break;
    }
    return "RowSmartPlacement";
  }

} // namespace bbai::configmenu

#endif // BLACKBOXAI_CONFIGSPELLING_HH
```

- [x] **Step 4: Run to verify pass**

```sh
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" blackboxai-ci:f44 bash -c '
  ninja -C build-f44 &&
  export XDG_RUNTIME_DIR=$(mktemp -d) && chmod 700 "$XDG_RUNTIME_DIR" &&
  WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-f44 -j 4'
```

Expected: all pass.

- [x] **Step 5: Commit**

```sh
git add src/ConfigSpelling.hh tests/unit/configmenu_test.cc
git commit -m "Configmenu: classic rc spellings for the persist path

Writer side of the focusModel composite + *Placement strings, header-only -
Config.cc parses these but is window-mgmt's file this wave, so the writer
can't live there. Round-tripped through updateRcKey + Config::load."
```

---

### Task 4: Server::setConfigOption - dispatch, persist, disabled-row guard

**Files:**
- Modify: `src/Server.hh` (decl near `applyConfig`, line ~280)
- Modify: `src/Server.cc` (include, `activateMenuItem` tail at 1476-1499, `handleMenuButton` at 1505-1516, new method after `applyStyleFile` ~line 383)
- Create: `tests/system/configmenu_test.cc`
- Modify: `tests/meson.build` (register, `text_env`)

**Interfaces:**
- Consumes: `ConfigOption` + `MenuItem::option` (Task 1), the mount (Task 2), `configmenu::focusModelValue`/`windowPlacementValue` (Task 3), `bbai::updateRcKey`, `Server::applyConfig()` (Server.hh:280), `bt::boolAsString` (toolkit/Resource.hh:43), Menu accessors `rectXForTest/rectYForTest/itemIndexAtGlobal/child/submenuOpenForTest/item/itemCount` (src/Menu.hh:29-50).
- Produces: `void Server::setConfigOption(ConfigOption opt)` (private, pinned name - menus appends its enum values' cases to this switch); `case MenuItem::Act::ConfigOption` in `activateMenuItem`; the disabled-row swallow in `handleMenuButton` (menus' gestures inherit it).

- [x] **Step 1: Write the failing test**

Create `tests/system/configmenu_test.cc`:

```cpp
// tests/system/configmenu_test.cc
// The Configuration submenu end-to-end: mount from a [config] menu file,
// click a row -> config_ flips in memory + persists to the rc with the
// classic spelling, reopen shows fresh checkmarks, disabled rows are dead.
// Click coordinates come from the menu's own hit-test accessors, never baked
// margin math - the menus slice may swap the pinned margins for style values
// and these tests must survive that re-bless without a rewrite (synthesis
// obligation). Every rc sets focusModel EXPLICITLY: window-mgmt flips the
// key-less default to SloppyFocus (locked) and lands before us.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Menu.hh"
#include "Config.hh"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <linux/input-event-codes.h>

using namespace bbai;

namespace {
  const char *kRc = "/tmp/bbai-configmenu-test.rc";
  const char *kMenu = "/tmp/bbai-configmenu-test.menu";

  void writeFile(const char *p, const std::string &body) {
    std::ofstream f(p, std::ios::trunc);
    f << body;
  }
  std::string slurp(const char *p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
  }
  void writeMenu() {
    writeFile(kMenu, "[begin] (Test)\n  [config] (Configuration)\n[end]\n");
  }
  void boot(Server &server) {
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
      server.dispatch();
    REQUIRE(server.titleFont()->height() == 18);  // gotcha #20: pinned font or the rows shift
  }

  // Row-center Y derived from the menu's own hit-test: scan the Y extent for
  // the band mapping to idx. itemIndexAtGlobal excludes separators only, so
  // disabled rows are findable too.
  int rowX(const Menu *m) { return m->rectXForTest() + 5; }
  int rowCenterY(const Menu *m, int idx) {
    int top = -1, bot = -1;
    for (int y = m->rectYForTest(); y < m->rectYForTest() + 600; ++y) {
      if (m->itemIndexAtGlobal(rowX(m), y) == idx) { if (top < 0) top = y; bot = y; }
      else if (top >= 0) break;
    }
    REQUIRE(top >= 0);
    return (top + bot) / 2;
  }
  void hoverRow(Server &server, const Menu *m, int idx) {   // hover-open handles cascades
    server.injectPointerMotionForTest(rowX(m), rowCenterY(m, idx));
  }
  void clickRow(Server &server, const Menu *m, int idx) {
    hoverRow(server, m, idx);
    server.injectPointerButtonForTest(BTN_LEFT, true);
  }

  // Right-click the desktop, hover-open Configuration (row 0). Child rows:
  // 0 Focus Model, 1 Window Placement, 2 separator, 3 Focus New Windows.
  Menu *openConfigMenu(Server &server) {
    server.injectPointerMotionForTest(600, 150);
    server.injectPointerButtonForTest(BTN_RIGHT, true);
    REQUIRE(server.menuOpenForTest());
    Menu *root = server.rootMenuForTest();
    hoverRow(server, root, 0);
    REQUIRE(root->submenuOpenForTest());
    return root->child();
  }
}

TEST_CASE("Focus New Windows: click flips config, persists the classic spelling, reopen unchecks") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  writeFile(kRc, "session.focusModel: ClickToFocus\n"
                 "session.focusNewWindows: True\n");
  writeMenu();

  Server server(/*headless=*/true, kRc);
  boot(server);
  server.setMenuFileForTest(kMenu);

  Menu *cfg = openConfigMenu(server);
  REQUIRE(cfg->itemCount() == 4);
  CHECK(cfg->item(3).checked);

  clickRow(server, cfg, 3);
  CHECK_FALSE(server.menuOpenForTest());           // toggle closes the chain (classic hideAll)
  CHECK_FALSE(server.config().focusNewWindows);    // in-memory flip, no reconfigure()
  CHECK(slurp(kRc).find("session.focusNewWindows: False\n") != std::string::npos);
  // updateRcKey rewrites ONE line - the user's other key survives verbatim.
  CHECK(slurp(kRc).find("session.focusModel: ClickToFocus\n") != std::string::npos);

  cfg = openConfigMenu(server);                    // rebuilt per open: fresh checkmarks
  CHECK_FALSE(cfg->item(3).checked);
  server.injectKeyForTest(XKB_KEY_Escape, 0, true);
  std::remove(kRc);
  std::remove(kMenu);
}

TEST_CASE("focus radios: Sloppy enables the raise rows; the composite spelling accretes") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  writeFile(kRc, "session.focusModel: ClickToFocus\n");
  writeMenu();

  Server server(/*headless=*/true, kRc);
  boot(server);
  server.setMenuFileForTest(kMenu);

  Menu *cfg = openConfigMenu(server);
  hoverRow(server, cfg, 0);                        // Focus Model hover-opens its cascade
  REQUIRE(cfg->submenuOpenForTest());
  Menu *fm = cfg->child();
  REQUIRE(fm->itemCount() == 4);
  CHECK(fm->item(0).checked);                      // ClickToFocus radio
  CHECK_FALSE(fm->item(2).enabled);                // AutoRaise locked out under CTF

  clickRow(server, fm, 1);                         // Sloppy Focus
  CHECK_FALSE(server.menuOpenForTest());
  CHECK(server.config().focusModel == FocusModel::SloppyFocus);
  CHECK(slurp(kRc).find("session.focusModel: SloppyFocus\n") != std::string::npos);

  cfg = openConfigMenu(server);
  hoverRow(server, cfg, 0);
  fm = cfg->child();
  CHECK(fm->item(1).checked);
  REQUIRE(fm->item(2).enabled);                    // now live
  clickRow(server, fm, 2);                         // Auto Raise on
  CHECK(server.config().autoRaise);
  CHECK(slurp(kRc).find("session.focusModel: SloppyFocus AutoRaise\n") != std::string::npos);
  std::remove(kRc);
  std::remove(kMenu);
}

TEST_CASE("placement radio: Cascade persists the classic *Placement spelling") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  writeFile(kRc, "session.focusModel: ClickToFocus\n"
                 "session.windowPlacement: RowSmartPlacement\n");
  writeMenu();

  Server server(/*headless=*/true, kRc);
  boot(server);
  server.setMenuFileForTest(kMenu);

  Menu *cfg = openConfigMenu(server);
  hoverRow(server, cfg, 1);                        // Window Placement cascade
  REQUIRE(cfg->submenuOpenForTest());
  Menu *wp = cfg->child();
  CHECK(wp->item(0).checked);                      // RowSmart from the rc
  clickRow(server, wp, 3);                         // Cascade
  CHECK(server.config().windowPlacement == WindowPlacement::Cascade);
  CHECK(slurp(kRc).find("session.windowPlacement: CascadePlacement\n") != std::string::npos);
  std::remove(kRc);
  std::remove(kMenu);
}

TEST_CASE("a disabled row neither dispatches nor dismisses (classic dead row)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  writeFile(kRc, "session.focusModel: ClickToFocus\n");
  writeMenu();

  Server server(/*headless=*/true, kRc);
  boot(server);
  server.setMenuFileForTest(kMenu);

  Menu *cfg = openConfigMenu(server);
  hoverRow(server, cfg, 0);
  Menu *fm = cfg->child();
  REQUIRE_FALSE(fm->item(2).enabled);              // Auto Raise, dead under CTF
  clickRow(server, fm, 2);
  CHECK(server.menuOpenForTest());                 // chain stays up
  CHECK_FALSE(server.config().autoRaise);          // nothing dispatched
  CHECK(slurp(kRc).find("AutoRaise") == std::string::npos);
  server.injectKeyForTest(XKB_KEY_Escape, 0, true);
  std::remove(kRc);
  std::remove(kMenu);
}

TEST_CASE("no rc: the toggle flips in memory, persist is a guarded no-op") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  writeMenu();

  Server server(/*headless=*/true);                // rc_path_ empty: headless never discovers HOME
  boot(server);
  server.setMenuFileForTest(kMenu);

  const bool before = server.config().focusNewWindows;
  Menu *cfg = openConfigMenu(server);
  clickRow(server, cfg, 3);
  CHECK(server.config().focusNewWindows == !before);   // live flip, no file, no crash
  std::remove(kMenu);
}
```

Register in `tests/meson.build` (after the `menu_file` block):

```meson
# wave-2 configmenu: Configuration submenu click-through -> in-memory flip +
# classic-spelling persist. text_env: click coordinates ride the pinned font.
configmenu_exe = executable('configmenu-test', files('system/configmenu_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('configmenu', configmenu_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)
```

- [x] **Step 2: Run to verify failure**

```sh
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" blackboxai-ci:f44 bash -c '
  ninja -C build-f44 &&
  export XDG_RUNTIME_DIR=$(mktemp -d) && chmod 700 "$XDG_RUNTIME_DIR" &&
  WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-f44 configmenu'
```

Expected: FAIL - clicking row 3 dispatches nothing (`Act::ConfigOption` has no case yet, falls to no switch match → compile error actually surfaces first if `-Wswitch` promotes; otherwise the `config().focusNewWindows` CHECK fails). Either failure mode is the right red.

- [x] **Step 3: Implement**

`src/Server.hh` - one declaration after `void applyConfig();` (line 280):

```cpp
    // Configuration-menu row dispatch: mutate config_ in memory, re-apply the
    // live knobs, persist ONE rc key with the classic spelling. Deliberately
    // NOT reconfigure() - that reloads the style, re-runs the rc rootCommand
    // and reverts unpersisted in-memory state on every toggle. Classic
    // Configmenu is toggle = set + save; this is that. menus appends its
    // ConfigOption values' cases here (append-only, tail).
    void setConfigOption(ConfigOption opt);
```

`src/Server.cc` - add `#include "ConfigSpelling.hh"` after `#include "Rootmenu.hh"`. New method after `applyStyleFile` (line ~383):

```cpp
  void Server::setConfigOption(ConfigOption opt) {
    switch (opt) {
    case ConfigOption::FocusClickToFocus:
      config_.focusModel = FocusModel::ClickToFocus;
      config_.autoRaise = false;    // classic: CTF forces both raise flags off
      config_.clickRaise = false;
      break;
    case ConfigOption::FocusSloppy:
      config_.focusModel = FocusModel::SloppyFocus;   // raise flags keep their values
      break;
    case ConfigOption::AutoRaise:   config_.autoRaise  = !config_.autoRaise;  break;
    case ConfigOption::ClickRaise:  config_.clickRaise = !config_.clickRaise; break;
    case ConfigOption::FocusNewWindows:
      config_.focusNewWindows = !config_.focusNewWindows;
      break;
    case ConfigOption::PlacementRowSmart: config_.windowPlacement = WindowPlacement::RowSmart; break;
    case ConfigOption::PlacementColSmart: config_.windowPlacement = WindowPlacement::ColSmart; break;
    case ConfigOption::PlacementCenter:   config_.windowPlacement = WindowPlacement::Center;   break;
    case ConfigOption::PlacementCascade:  config_.windowPlacement = WindowPlacement::Cascade;  break;
    }

    std::string key, value;
    switch (opt) {
    case ConfigOption::FocusClickToFocus:
    case ConfigOption::FocusSloppy:
    case ConfigOption::AutoRaise:
    case ConfigOption::ClickRaise:
      key = "session.focusModel";
      value = configmenu::focusModelValue(config_);
      break;
    case ConfigOption::FocusNewWindows:
      key = "session.focusNewWindows";
      value = bt::boolAsString(config_.focusNewWindows);
      break;
    case ConfigOption::PlacementRowSmart:
    case ConfigOption::PlacementColSmart:
    case ConfigOption::PlacementCenter:
    case ConfigOption::PlacementCascade:
      key = "session.windowPlacement";
      value = configmenu::windowPlacementValue(config_.windowPlacement);
      break;
    }

    // Nothing WE mutate feeds applyConfig today (focus/placement are read
    // live by their consumers) - the call is the seam contract, so menus'
    // Toolbar*/Slit* values apply for free when they extend the switch.
    applyConfig();
    // Loud-but-nonfatal persist, applyStyleFile precedent (Server.cc:380).
    // The !empty guard matters: a headless test Server has no rc path and
    // updateRcKey("") would create a file literally named "".
    if (!rc_path_.empty() && !bbai::updateRcKey(rc_path_, key, value))
      fprintf(stderr, "blackboxai: could not persist %s to %s\n",
              key.c_str(), rc_path_.c_str());
  }
```

`activateMenuItem` (line ~1493) - add the case in the submenu-marker block's neighborhood, keeping the marker comment intact:

```cpp
    case MenuItem::Act::WorkspacesMenu:      // submenu markers - a Submenu is
    case MenuItem::Act::ConfigMenu:  break;  // never dispatched as a command
    case MenuItem::Act::ConfigOption:    setConfigOption(copy.option); break;
```

`handleMenuButton` (line ~1505) - disabled rows must be dead, not dispatched. `itemIndexAtGlobal` filters separators only (Menu.geom.hh `itemAt`), and today a disabled non-submenu row would fall through to `activateMenuItem`. Guard first:

```cpp
      const int idx = m->itemIndexAtGlobal(x, y);
      if (idx >= 0) {
        if (!m->item(idx).selectable()) return;   // disabled row: swallow, keep the chain open
        if (m->item(idx).kind == MenuItem::Kind::Submenu) {
          m->openSubmenuAt(idx);
          return;
        }
        activateMenuItem(m->item(idx));
        return;
      }
```

(The old `&& m->item(idx).selectable()` on the Submenu line is subsumed - remove it. Behavior change vs wave-1: clicking a disabled submenu row used to dismiss the whole chain through the no-op `Act::ConfigMenu` dispatch; now it's inert. That was placeholder-era behavior nobody pinned - the disabled-row test in Step 1 pins the new, classic-correct one.)

- [x] **Step 4: Run the full gate**

```sh
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" blackboxai-ci:f44 bash -c '
  ninja -C build-f44 &&
  export XDG_RUNTIME_DIR=$(mktemp -d) && chmod 700 "$XDG_RUNTIME_DIR" &&
  WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-f44 -j 4'
```

Expected: all pass, including the new `configmenu` suite entry. `git status tests/golden/` unchanged since Task 2.

- [x] **Step 5: Commit**

```sh
git add src/Server.hh src/Server.cc tests/system/configmenu_test.cc tests/meson.build
git commit -m "Server: setConfigOption - flip a knob in memory, applyConfig, persist one rc key

Classic toggle=set+save, NOT reconfigure(): a reconfigure here would reload
the style, re-run the rc rootCommand and clobber unpersisted in-memory
state on every toggle. Persist is guarded on rc_path_ (headless has none)
and loud-but-nonfatal, applyStyleFile precedent. handleMenuButton also
gains the missing disabled-row swallow - itemIndexAtGlobal only filters
separators, so a dead row would have dispatched."
```

---

### Task 5: Remove Last Workspace re-homes its tenants

**Files:**
- Modify: `src/Server.hh` (decl next to `setCurrentWorkspace`), `src/Server.cc` (new method + `activateMenuItem:1480` rewire)
- Create: `tests/system/workspace_remove_test.cc`
- Modify: `tests/meson.build` (register, `text_env`)

**Interfaces:**
- Consumes: `WorkspaceModel::{count,current,focused,setFocused,removeLastWorkspace}` (src/Workspace.hh), `View::{workspace,setWorkspace,setOnWorkspace}` (src/View.hh:44-47), `Server::setCurrentWorkspace` (Server.cc:1265, gotcha #29 semantics), `Toolbar::redrawWorkspaceLabel`.
- Produces: `void Server::removeLastWorkspaceAndRehome()` (private; `Act::RemoveWorkspace`'s new target). The rc/applyConfig path stays grow-only - locked.

- [x] **Step 1: Write the failing test**

Create `tests/system/workspace_remove_test.cc`:

```cpp
// tests/system/workspace_remove_test.cc
// Remove Last Workspace with tenants: views re-home to the last survivor,
// the current workspace follows when it is the one dying, and focus repair
// keeps a live handle (gotcha #29). Driven through the real menu (keyboard
// nav - no coordinates), because Act::RemoveWorkspace IS the only shrink
// path: the rc/applyConfig path stays grow-only (locked).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"
#include "Menu.hh"

#include <cstdio>
#include <cstdlib>
#include <fstream>

using namespace bbai;

namespace {
  const char *kRc = "/tmp/bbai-wsremove-test.rc";

  void boot(Server &server) {
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
      server.dispatch();
  }
  void settle(Server &server, test::TestClient &c) {
    auto mapped = [&] {
      const auto &v = server.viewsForTest();
      return !v.empty() && v.back()->isMapped();
    };
    for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); server.dispatch(); c.pump(); }
    REQUIRE(mapped());
    for (int i = 0; i < 30; ++i) { c.flush(); server.dispatch(); c.pump(); }
  }

  // Open the in-code root menu, keyboard-walk to Workspaces (row 3), Return
  // to cascade, walk to the last child row (Remove Last Workspace), Return.
  void removeLastViaMenu(Server &server) {
    server.injectPointerMotionForTest(700, 300);
    server.injectPointerButtonForTest(BTN_RIGHT, true);
    REQUIRE(server.menuOpenForTest());
    Menu *root = server.rootMenuForTest();
    // in-code menu rows: 0 kitty, 1 xterm, 2 sep, 3 Workspaces, 4 sep, 5 Restart, 6 Exit
    for (int i = 0; i < 10 && root->activeIndex() != 3; ++i)
      server.injectKeyForTest(XKB_KEY_Down, 0, true);
    REQUIRE(root->activeIndex() == 3);
    server.injectKeyForTest(XKB_KEY_Return, 0, true);
    Menu *ws = root->child();
    REQUIRE(ws != nullptr);
    const int last = ws->itemCount() - 1;             // Remove Last Workspace
    for (int i = 0; i < 16 && ws->activeIndex() != last; ++i)
      server.injectKeyForTest(XKB_KEY_Down, 0, true);
    REQUIRE(ws->activeIndex() == last);
    server.injectKeyForTest(XKB_KEY_Return, 0, true);
    REQUIRE_FALSE(server.menuOpenForTest());
  }
}

TEST_CASE("remove-last re-homes tenants, follows a dying current, repairs focus") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  { std::ofstream f(kRc, std::ios::trunc); f << "session.screen0.workspaces: 3\n"; }

  Server server(/*headless=*/true, kRc);
  boot(server);
  server.setMenuFileForTest("");   // pin the in-code menu (Workspaces submenu lives there)
  REQUIRE(server.workspaces().count() == 3u);

  // A on ws0 (current), B on ws2.
  test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  REQUIRE(a.ok());
  settle(server, a);
  View *va = server.viewsForTest()[0].get();

  server.setCurrentWorkspace(2);
  test::TestClient b(server.socketName(), 0xFF00FF00u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  REQUIRE(b.ok());
  settle(server, b);
  View *vb = server.viewsForTest()[1].get();
  REQUIRE(vb->workspace() == 2u);

  // Case 1: remove while current is elsewhere - B re-homes to ws1, hidden.
  server.setCurrentWorkspace(0);
  removeLastViaMenu(server);
  CHECK(server.workspaces().count() == 2u);
  CHECK(vb->workspace() == 1u);
  CHECK(server.currentWorkspaceForTest() == 0u);
  CHECK(server.focusedViewForTest() == va);         // ws0's focus untouched

  // The re-homed view is really there: switching to the survivor finds it.
  server.setCurrentWorkspace(1);
  CHECK(server.focusedViewForTest() == vb);

  // Case 2: remove while standing ON the dying workspace with focus there -
  // current follows to the survivor and the focused view keeps focus
  // (gotcha #29: aim the survivor's memory before the switch).
  removeLastViaMenu(server);
  CHECK(server.workspaces().count() == 1u);
  CHECK(server.currentWorkspaceForTest() == 0u);
  CHECK(vb->workspace() == 0u);
  CHECK(server.focusedViewForTest() == vb);

  // Floor: the last workspace never goes away.
  removeLastViaMenu(server);
  CHECK(server.workspaces().count() == 1u);
  CHECK(server.focusedViewForTest() == vb);
  std::remove(kRc);
}
```

Register in `tests/meson.build` (after the configmenu block):

```meson
# wave-2 configmenu: workspace shrink re-home via the explicit RemoveWorkspace
# action (the rc path stays grow-only).
workspace_remove_exe = executable('workspace-remove-test',
  files('system/workspace_remove_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('workspace_remove', workspace_remove_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)
```

- [x] **Step 2: Run to verify failure**

```sh
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" blackboxai-ci:f44 bash -c '
  ninja -C build-f44 &&
  export XDG_RUNTIME_DIR=$(mktemp -d) && chmod 700 "$XDG_RUNTIME_DIR" &&
  WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-f44 workspace_remove'
```

Expected: FAIL at `CHECK(vb->workspace() == 1u)` - today's `Act::RemoveWorkspace` calls the bare model op and B's index still says 2 (a dead workspace: the window is lost forever, which is exactly the parked debt).

- [x] **Step 3: Implement**

`src/Server.hh` - after the `setCurrentWorkspace` declaration (line ~88):

```cpp
    // Explicit workspace removal (menu Act::RemoveWorkspace): re-home the
    // dying workspace's views to the last survivor BEFORE the model pops the
    // slot, follow with the current workspace when it is the one dying, and
    // repair focus memory per gotcha #29. The rc/applyConfig path stays
    // grow-only (locked) - this is the ONLY shrink path.
    void removeLastWorkspaceAndRehome();
```

`src/Server.cc` - after `setCurrentWorkspace`'s body (line ~1290):

```cpp
  void Server::removeLastWorkspaceAndRehome() {
    const unsigned n = workspaces_.count();
    if (n <= 1) return;                       // model floor: never below 1
    const unsigned dying = n - 1, survivor = n - 2;
    const bool current_on_dying = (workspaces_.current() == dying);
    // Captured BEFORE re-homing: afterwards every tenant claims the survivor.
    View *keep = (focused_view && focused_view->workspace() == dying)
                     ? focused_view : nullptr;

    for (auto &v : views)
      if (v->workspace() == dying) v->setWorkspace(survivor);

    // Gotcha #29: setCurrentWorkspace's restore branch focuses the incoming
    // workspace's REMEMBERED view - point the survivor's memory at the view
    // that actually holds focus first, so the restore lands on it (focusView
    // early-returns) instead of yanking focus to a stale memory.
    if (keep) workspaces_.setFocused(survivor, keep);

    if (current_on_dying) {
      setCurrentWorkspace(survivor);   // full switch: show/hide + focus restore + label
    } else {
      // No switch happened: sync visibility for the re-homed views (hidden
      // unless the survivor IS current). Idempotent for existing tenants.
      for (auto &v : views)
        if (v->workspace() == survivor)
          v->setOnWorkspace(survivor == workspaces_.current());
    }

    workspaces_.removeLastWorkspace();        // pops the dying slot, clamps current_
    if (toolbar_) toolbar_->redrawWorkspaceLabel();
  }
```

`activateMenuItem` (line 1480) - rewire:

```cpp
    case MenuItem::Act::RemoveWorkspace: removeLastWorkspaceAndRehome(); break;
```

- [x] **Step 4: Run the full gate**

```sh
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" blackboxai-ci:f44 bash -c '
  ninja -C build-f44 &&
  export XDG_RUNTIME_DIR=$(mktemp -d) && chmod 700 "$XDG_RUNTIME_DIR" &&
  WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-f44 -j 4'
```

Expected: all pass - including the pre-existing menu/workspace suites (the empty-workspace RemoveWorkspace path behaves as before: no tenants, model op + label).

- [x] **Step 5: Commit**

```sh
git add src/Server.hh src/Server.cc tests/system/workspace_remove_test.cc tests/meson.build
git commit -m "Server: Remove Last Workspace re-homes its tenants to the survivor

The bare model op stranded views on a dead index forever - the debt the
applyConfig comment parked on this slice. Re-home first (both indices still
valid), aim the survivor's focus memory at the focused tenant before any
switch (gotcha #29), then a real setCurrentWorkspace when the current
workspace is the one dying. rc path stays grow-only - this action is the
only shrink."
```

---

### Task 6: strftimeFormat drives the toolbar clock

**Files:**
- Modify: `src/Toolbar.cc:74-76` (one line)
- Create: `tests/system/clock_format_test.cc`
- Modify: `tests/meson.build` (register, `text_env`)

**Interfaces:**
- Consumes: `bt::formatClock(int64_t, const char *fmt)` (toolkit/Clock.hh:42, gmtime_r-based), `Config::strftimeFormat` (parsed since wave 1, default `"%I:%M %p"` - identical to formatClock's default, which is why zero goldens churn), `Toolbar::clockText` (public, Toolbar.hh:60), the VirtualClock epoch 14:05:00 UTC (Server.cc:228).
- Produces: nothing new - a live rc knob. NOTE for the merge slot: `Config.hh:84`'s "parse-only this wave (locked)" comment goes stale here; it is window-mgmt's file, so the one-line comment fix happens in the train commit (header checklist), not in this worktree.

- [x] **Step 1: Write the failing test**

Create `tests/system/clock_format_test.cc`:

```cpp
// tests/system/clock_format_test.cc
// session.screen0.strftimeFormat reaches the toolbar clock. The compiled
// default format equals bt::formatClock's default, so every existing clock
// golden holds byte-identically - pinned here as its own case. Custom cases
// use %H/%M only (locale-immune; %p would bend under a non-C LC_TIME).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Toolbar.hh"

#include <cstdio>
#include <cstdlib>
#include <fstream>

using namespace bbai;

namespace {
  const char *kRc = "/tmp/bbai-clockformat-test.rc";
  void writeRc(const std::string &body) {
    std::ofstream f(kRc, std::ios::trunc);
    f << body;
  }
  void boot(Server &server) {
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
      server.dispatch();
    REQUIRE(server.toolbarForTest() != nullptr);
  }
}

TEST_CASE("a custom strftimeFormat renders in the toolbar clock") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  writeRc("session.screen0.strftimeFormat: %H:%M\n");

  Server server(/*headless=*/true, kRc);
  boot(server);
  CHECK(server.toolbarForTest()->clockText() == "14:05");   // VirtualClock epoch, 24h
  server.advanceClockForTest(60);
  CHECK(server.toolbarForTest()->clockText() == "14:06");   // the tick re-renders through it
  std::remove(kRc);
}

TEST_CASE("no key: the classic default format is unchanged (existing goldens hold)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  boot(server);
  CHECK(server.toolbarForTest()->clockText() == "02:05 PM");
}

TEST_CASE("reconfigure() re-reads the format and re-renders the clock") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  writeRc("session.styleFile: data/styles/Results\n");   // exact style so reconfigure() == true

  Server server(/*headless=*/true, kRc);
  boot(server);
  CHECK(server.toolbarForTest()->clockText() == "02:05 PM");

  writeRc("session.styleFile: data/styles/Results\n"
          "session.screen0.strftimeFormat: %H.%M\n");
  CHECK(server.reconfigure());   // restyle -> toolbar rebuild -> redrawClock
  CHECK(server.toolbarForTest()->clockText() == "14.05");
  std::remove(kRc);
}
```

Register in `tests/meson.build` (after the workspace_remove block):

```meson
# wave-2 configmenu: strftimeFormat rendering swap (default format identical -
# the zero-golden-churn claim is the full suite staying green).
clock_format_exe = executable('clock-format-test', files('system/clock_format_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('clock_format', clock_format_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)
```

- [x] **Step 2: Run to verify failure**

```sh
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" blackboxai-ci:f44 bash -c '
  ninja -C build-f44 &&
  export XDG_RUNTIME_DIR=$(mktemp -d) && chmod 700 "$XDG_RUNTIME_DIR" &&
  WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-f44 clock_format'
```

Expected: FAIL - `clockText() == "14:05"` gets `"02:05 PM"` (the format never leaves the parser today).

- [x] **Step 3: Implement**

`src/Toolbar.cc:74-76`:

```cpp
  std::string Toolbar::clockText(void) const {
    return bt::formatClock(server_.clock().wallSeconds(),
                           server_.config().strftimeFormat.c_str());
  }
```

- [x] **Step 4: Run the full gate**

```sh
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" blackboxai-ci:f44 bash -c '
  ninja -C build-f44 &&
  export XDG_RUNTIME_DIR=$(mktemp -d) && chmod 700 "$XDG_RUNTIME_DIR" &&
  WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-f44 -j 4'
```

Expected: ALL pass - especially `toolbar`, `clock_seam`, and every golden test: the default format string is byte-identical to `formatClock`'s default, so `m4-toolbar*.png` and friends must not move. Any golden diff here means the one-liner did something else - stop.

- [x] **Step 5: Commit**

```sh
git add src/Toolbar.cc tests/system/clock_format_test.cc tests/meson.build
git commit -m "Toolbar: clock renders config strftimeFormat

One line - formatClock always took the format, the toolbar just never
passed it. Default string is identical so the clock goldens hold; the
custom-format fixture is the new coverage. %H/%M cases only in tests -
%p is locale-bent and CI shouldn't care."
```

---

### Task 7: Watch-item fix - headless hides the install-prefix default style

**Files:**
- Modify: `src/Server.hh` (member + test lever), `src/Server.cc` (macro guard, ctor init, `loadStyleWithFallback:307-317`)
- Test: `tests/system/retheme_test.cc` (comment at 137-138 + new middle-rung case), `tests/system/style_boot_test.cc:73-83` (comment)

**Interfaces:**
- Consumes: `loadStyleWithFallback` (Server.cc:307), `Style::load`/`Style::builtin`, `BBAI_DEFAULT_STYLE` (src/meson.build:8 - baked into lib objects only; tests can't see or shadow it, which is WHY the fix must be production-side).
- Produces: `void Server::setDefaultStyleForTest(const std::string &path)` + member `std::string default_style_path_` (empty on headless). The two watch-item assertions become install-agnostic with NO assertion changes.

**Mechanism (one, for both files):** headless Servers treat the compiled install-prefix default style as absent - both faces of the macro: the ladder's middle rung comes from `default_style_path_` (empty on headless), and an incoming request for the macro path itself (Config defaults `styleFile` to it when the rc names none) is refused. Same stance as the ctor's rc discovery: a box where the product is installed must not leak prefix state into the golden suite. Real backends are untouched. Bonus: the test lever makes the ladder's middle rung CI-coverable for the first time (today it only runs on installed boxes, i.e. never in CI).

- [x] **Step 1: Write the failing test**

Append to `tests/system/retheme_test.cc`:

```cpp
TEST_CASE("style ladder middle rung: a pinned default style catches the fallback") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  writeRc("session.styleFile: /nonexistent/style\n");

  Server server(/*headless=*/true, kRc);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();
  // Headless boots with an EMPTY default rung: builtin, on any box.
  CHECK(server.currentStyle()->sourcePath().empty());

  // Pin a fake "installed default": the middle rung catches the fallback.
  server.setDefaultStyleForTest("data/styles/Results");
  CHECK_FALSE(server.reconfigure());   // the exact style still failed
  CHECK(server.currentStyle()->sourcePath() == "data/styles/Results");

  // Clear it: back to the builtin rung.
  server.setDefaultStyleForTest("");
  CHECK_FALSE(server.reconfigure());
  CHECK(server.currentStyle()->sourcePath().empty());
  std::remove(kRc);
}
```

- [x] **Step 2: Run to verify failure**

```sh
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" blackboxai-ci:f44 bash -c 'ninja -C build-f44'
```

Expected: compile FAILURE - `setDefaultStyleForTest` is not a member of `bbai::Server`.

- [x] **Step 3: Implement**

`src/Server.cc` - add the empty-macro guard near the top (Config.cc:23-25 pattern), right after the includes:

```cpp
// Install-path default injected by src/meson.build (lib objects only).
#ifndef BBAI_DEFAULT_STYLE
#define BBAI_DEFAULT_STYLE ""
#endif
```

Ctor (line ~67), FIRST thing in the body - before `Config::load` so the very first `loadStyleWithFallback` already sees it:

```cpp
    // Headless treats the install-prefix default style as absent - a box
    // where the product IS installed must not leak prefix state into the
    // golden suite (same stance as rc discovery below). Tests pin a fake
    // default via setDefaultStyleForTest to exercise the middle rung.
    if (!headless) default_style_path_ = BBAI_DEFAULT_STYLE;
```

`loadStyleWithFallback` (Server.cc:307-317) - replace:

```cpp
  std::shared_ptr<const Style> Server::loadStyleWithFallback(const std::string &path,
                                                             bool *exact_ok) {
    if (exact_ok) *exact_ok = true;
    // Second face of the headless default-hiding (ctor): when the rc names no
    // style, Config itself defaults styleFile to BBAI_DEFAULT_STYLE - refuse
    // that exact path too, or an installed prefix reaches the goldens anyway.
    const bool hidden = headless && !path.empty()
                        && path == std::string(BBAI_DEFAULT_STYLE);
    if (!hidden)
      if (auto s = Style::load(path, config_.rootCommand)) return s;
    if (exact_ok) *exact_ok = false;
    fprintf(stderr, "blackboxai: style '%s' unreadable, falling back\n", path.c_str());
    if (!default_style_path_.empty())
      if (auto s = Style::load(default_style_path_, config_.rootCommand)) return s;
    return Style::builtin(config_.rootCommand);
  }
```

`src/Server.hh` - test lever next to `setMenuFileForTest` (line ~129):

```cpp
    // Pin the style ladder's middle rung (empty = skip straight to builtin).
    // Headless boots it empty so an installed prefix can't leak into goldens;
    // this is how tests exercise the default-style rung at all.
    void setDefaultStyleForTest(const std::string &path) { default_style_path_ = path; }
```

and the member next to `rc_path_` (line ~282):

```cpp
    std::string default_style_path_;   // style ladder middle rung; empty on headless
```

Add anchoring comments in the two watch-item tests (assertions unchanged - they are now install-agnostic):

`tests/system/retheme_test.cc:137-138`:

```cpp
  writeRc("session.styleFile: /nonexistent/style\n");
  CHECK_FALSE(server.reconfigure());
  // builtin rung on ANY box: headless hides the install-prefix default
  // (Server::loadStyleWithFallback), so an installed product can't flip this.
  CHECK(server.currentStyle()->sourcePath().empty());
```

`tests/system/style_boot_test.cc:76-78`:

```cpp
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  // builtin on ANY box: Config defaults styleFile to the compiled prefix
  // path, and headless refuses exactly that path (install-prefix hiding).
  CHECK(server.currentStyle()->sourcePath().empty());   // builtin rung
```

- [x] **Step 4: Run the full gate**

```sh
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" blackboxai-ci:f44 bash -c '
  ninja -C build-f44 &&
  export XDG_RUNTIME_DIR=$(mktemp -d) && chmod 700 "$XDG_RUNTIME_DIR" &&
  WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-f44 -j 4'
```

Expected: all pass. In CI the container never has an installed prefix, so pre-existing behavior is bit-identical - the change only bites on installed dev boxes. **Verification limit, stated:** the real-backend branch (`default_style_path_` = the macro) can't run in CI; it is exercised by reasoning plus the next `meson install` hand-check on the user's TTY box. The middle-rung logic itself IS now covered via the lever.

- [x] **Step 5: Commit**

```sh
git add src/Server.hh src/Server.cc tests/system/retheme_test.cc tests/system/style_boot_test.cc
git commit -m "Server: headless hides the install-prefix default style

retheme_test:138 and style_boot_test:78 assert the builtin rung, which held
only on boxes where the product isn't installed - BBAI_DEFAULT_STYLE would
otherwise satisfy the ladder (or Config's styleFile default, the sneakier
face). Headless now treats the prefix as absent, same stance as rc
discovery. The new setDefaultStyleForTest lever makes the middle rung
CI-coverable for the first time; the real-backend face stays a TTY check."
```

---

### Task 8: Configmenu golden + slice gate

**Files:**
- Modify: `tests/system/configmenu_test.cc` (one golden case)
- Create: `tests/golden/v1-configmenu.png` (blessed)
- Possibly test-only additions if gcovr flags a branch (no production changes in this task)

**Interfaces:**
- Consumes: everything above; `test::captureFrame`/`compareGolden` (tests/harness/HeadlessFixture.hh).
- Produces: the slice's one NEW golden; the pre-merge evidence bundle (suite green, coverage >= 80, golden delta = exactly the declared set).

- [ ] **Step 1: Write the golden test**

Append to `tests/system/configmenu_test.cc`:

```cpp
TEST_CASE("golden: Configuration submenu + Focus Model cascade (builtin style)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  // Explicit focus model: merge-stable across window-mgmt's default flip,
  // and it puts a checkmark + one disabled pair in frame (drawCheck gutter,
  // frameDisabled text - the two renderings this slice leans on).
  writeFile(kRc, "session.focusModel: SloppyFocus AutoRaise\n");
  writeMenu();

  Server server(/*headless=*/true, kRc);
  boot(server);
  server.setMenuFileForTest(kMenu);

  Menu *cfg = openConfigMenu(server);
  hoverRow(server, cfg, 0);                        // Focus Model cascade open
  REQUIRE(cfg->submenuOpenForTest());
  CHECK(test::compareGolden(test::captureFrame(server),
                            "tests/golden/v1-configmenu.png", 2, 40));
  server.injectKeyForTest(XKB_KEY_Escape, 0, true);
  std::remove(kRc);
  std::remove(kMenu);
}
```

- [ ] **Step 2: Bless it, eyeball it, run it clean**

```sh
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" blackboxai-ci:f44 bash -c '
  ninja -C build-f44 &&
  export XDG_RUNTIME_DIR=$(mktemp -d) && chmod 700 "$XDG_RUNTIME_DIR" &&
  BLESS=1 WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-f44 configmenu &&
  WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-f44 configmenu'
```

Expected: both runs pass; `tests/golden/v1-configmenu.png` created. Open the PNG and confirm: root menu + Configuration cascade + Focus Model cascade; Sloppy Focus and Auto Raise carry checkmarks; nothing rendered grey EXCEPT nothing (all four focus rows are enabled under sloppy - the grey pair shows in the disabled-row TEST, not this golden; what matters here is checks + arrows + three-deep cascade geometry).

- [ ] **Step 3: Full gate + coverage + golden-delta audit**

```sh
docker run --rm --shm-size=1g -v /home/hila/proj:/home/hila/proj -w "$WT" blackboxai-ci:f44 bash -c '
  ninja -C build-f44 &&
  export XDG_RUNTIME_DIR=$(mktemp -d) && chmod 700 "$XDG_RUNTIME_DIR" &&
  WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build-f44 -j 4 &&
  gcovr -r . build-f44 --filter toolkit/ --filter src/ --txt --fail-under-line=80'
git -C "$WT" status --short tests/golden/
```

Expected: every suite green; gcovr exits 0 at the project's usual ~91% (specifically `src/Rootmenu.cc` and the new Server methods near-full - they are pure/headless by construction). Golden delta across the WHOLE branch = exactly: `m5-menufile.png` (M, Task 2), `m5-menufile-cascade.png` (M, Task 2), `v1-configmenu.png` (A, this task). Anything else fails the slice's zero-churn promise - fix before commit.

- [ ] **Step 4: Self-check before hand-off (run, don't skip)**

- Grep the diff for territory: `git diff <base> --stat` must show NO hunks in `src/Config.cc`, `src/Config.hh`, `src/Menu.cc`, `src/Menu.hh`, `src/Menu.geom.hh`, `src/View.*`, `src/Style.*`, `src/SniHost.*`.
- Seam spellings verbatim: `Act::ConfigOption` right after `ConfigMenu`; `ConfigOption` values in the pinned order ending at `PlacementCascade`; `Server::setConfigOption(ConfigOption)`; `rootmenu::buildConfigSubmenu(const Config &)`.
- No test asserts a default-constructed Config's focus model (grep `focusModel` in tests touched - every use explicit).

- [ ] **Step 5: Commit**

```sh
git add tests/system/configmenu_test.cc tests/golden/v1-configmenu.png
git commit -m "Configmenu golden: three-deep cascade with live checkmarks

One new golden for the wave (sanctioned); menus' margin re-bless may sweep
it later - that's theirs. Slice gate: full suite, coverage ~91, golden
delta = the two declared m5-menufile flips + this file."
```

---

## Self-review (done while writing; re-run after execution)

1. **Mandate coverage:** Configuration submenu UI + persist (Tasks 1-4), `Act::ConfigOption` + enum + `setConfigOption` dispatch with `applyConfig` + guarded `updateRcKey` (Tasks 1, 4), `buildFromParsed` Config param at exactly the two verified call sites (Task 2), workspace shrink re-home via explicit RemoveWorkspace with grow-only rc path (Task 5), strftimeFormat render swap with fixture-not-re-bless (Task 6), install-prefix watch-item at retheme:138 + style_boot:78 with one mechanism (Task 7), accessor-derived click coordinates (Task 4 helpers), sloppy-default merge-slot assert (header checklist). No gaps found.
2. **Placeholder scan:** every code step carries the actual code; the only conditional step is Task 8's "add unit cases if gcovr flags a branch," bounded to test-only.
3. **Type consistency:** `ConfigOption` (namespace `bbai`, in MenuItem.hh) is the type in `MenuItem::option`, `setConfigOption`, and the builder helper; `buildFromParsed(const std::vector<MenuItem>&, const WorkspaceModel&, const Config&)` matches between Rootmenu.hh, Rootmenu.cc, Server.cc:1397 and all 5 wire-test uses; `configmenu::focusModelValue(const Config&)` / `windowPlacementValue(WindowPlacement)` match Task 3's header and Task 4's calls; `removeLastWorkspaceAndRehome()` matches Server.hh/cc/dispatch. One bug caught in review: an earlier draft asserted `CHECK(server.reconfigure())` in the clock-format test with a style-less rc - false even today (default-style rung fails in CI); fixed by pinning `session.styleFile: data/styles/Results` in that fixture.
