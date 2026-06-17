# BlackboxAI M5(Phase 2) — Deferred M4 Features Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Commit messages: author with the **hila-voice** skill (personal repo).

**Goal:** On top of the green wlroots-0.20 port (`b45727c`), complete the four deferred M4 features — active/inactive frame-focus swap, cascade submenus, iconbar + iconify/maximize, toolbar auto-hide + placements — each behavior added by its own TDD commit with goldens changed only with intent.

**Architecture:** Stack the four features in the order **F1 focus-swap → F3 cascade submenus → F4 iconbar+iconify → F2 toolbar auto-hide** (chosen to minimize golden re-blessing and sequence shared-file edits). Each feature reuses existing seams: the M1 renderer seam (`bt::Texture → bt::Image::renderBuffer → DataBuffer → wlr_scene_buffer`), the pure geometry headers (`Frame.hh`, `Menu.geom.hh`, `Toolbar.geom.hh`), the modal-menu gate (`active_menu_`), and the injectable `Timer`/`VirtualClock`.

**Tech Stack:** C++20, wlroots 0.20, meson+ninja, doctest (vendored), fcft text, headless+pixman golden-PNG tests, gcovr (≥80% gate).

---

## Decisions baked in (resolved during brainstorming/design-research)

- **No auto-focus on map.** A freshly-mapped, unclicked window stays unfocused and therefore renders the *inactive* look — faithful to classic Blackbox (`session.focusNewWindows` defaults off; it is a **Configmenu** toggle, which is M5). This preserves M4's focus-memory model and `workspace_switch_test`'s "no remembered focus → topmost" case. The `focusNewWindows` knob is deferred to M5's Configmenu. `frame_test` gains a focus-click so its `m3-frame-ssd.png` reference stays the *active* look (see Task F1.3).
- **Toolbar auto-hide default OFF** (always visible); auto-hide is opt-in via a test seam until M5 wires config. Existing toolbar goldens stay unchanged.
- **Iconified-Windows menu opens via middle-click on the desktop background AND a keybinding** `Mod4+Alt+T` (full-screen windows can leave no reachable desktop). The **root menu** has the same reachability gap, so it also gains a keybinding `Mod4+Space` (Task F3.x).
- **Inactive palette**: derive from the classic Gray style — desaturated/darker title/label/handle/grip/button gradients, mid-grey inactive label-text + button glyph (vs active black), slightly lighter inactive border. Concrete hex picked and blessed in Task F1.4.
- **Auto-hide delay** `kHideDelayMs = 250`. **Maximize work area** = active output minus `toolbar::kBarHeight` (single M1 output; no strut abstraction — M5). **Off-screen submenu left-flip** deferred; rely on `Menu::show`'s existing on-screen clamp.
- **Icon-menu visual golden**: not blessed; a non-visual item-count + deiconify test suffices.

## Reconciliation note (design-research vs current code)

`MenuItem` (`src/MenuItem.hh`) already has `Kind::Submenu`, `Act::NewWorkspace`/`Act::RemoveWorkspace`, and a vestigial `Menu *sub` runtime pointer. The cascade work therefore: (a) adds `std::vector<MenuItem> submenu_items;` to `MenuItem` (pure child data, set by `Rootmenu::build` which has no `Server&`), (b) adds runtime `parent_`/`child_` to `Menu` (the lazily-built child), and (c) drops the unused `Menu *sub` field to avoid two child mechanisms. Do NOT add the research's parallel model — use `submenu_items` + `Menu::child_`.

---

## File structure

**Modified (existing):**
- `src/Decoration.hh` / `src/Decoration.cc` — focused/unfocused palette pairs; `rebuild` gains `bool focused`; `Part` enum split into `IconifyButton`/`MaximizeButton`/`CloseButton`.
- `src/View.hh` / `src/View.cc` — `focused_` + `setFocused`/`isFocused`; `iconified_`/`maximized_` + premax geometry; `applyVisibility()`.
- `src/Server.hh` / `src/Server.cc` — restyle calls in `focusView`/`clearFocus`; `liveMenu()` deepest-child routing; per-button press/release dispatch; `iconifyView`/`deiconifyView` + `icons_`; `rehomeFocusAfterLoss` helper; toolbar edge-trigger; two new keybindings.
- `src/MenuItem.hh` — `submenu_items`; `Act::Deiconify` + `void *target`.
- `src/Menu.hh` / `src/Menu.cc` — submenu arrow; `parent_`/`child_`; `openSubmenu`/`closeSubmenu`; introspection accessors.
- `src/Rootmenu.cc` — Workspaces submenu replaces the inline workspace rows.
- `src/Toolbar.hh` / `src/Toolbar.cc` / `src/Toolbar.geom.hh` — `Placement`; `hiddenBarRect`; auto-hide state + hide timer; window-label-on-focus.
- `src/Keybindings.hh` — `Mod4+Alt+T` (icon menu), `Mod4+Space` (root menu).
- Existing tests updated: `frame_test.cc` (focus click), `hittest_test.cc` (per-button parts), `menu_action_test.cc` + `rootmenu_test.cc` (collapsed indices).

**Created:**
- `tests/unit/decoration_palette_test.cc`, `tests/system/toolbar_placement_test.cc`
- Goldens: `tests/golden/m4-focus-swap.png`, `m4-rootmenu-submenu.png`, `m4-maximized.png`, `m4-toolbar-winlabel.png`, `m4-toolbar-topcenter.png`, `m4-toolbar-hidden.png`; **re-bless** `m4-rootmenu.png`, `m4-rootmenu-hilite.png`.

**Per-task commands** (run from repo root; build dir already coverage-configured):
- Build: `ninja -C build`
- One test: `meson test -C build blackboxai:<name>` (env incl. `text_env`/`XDG_RUNTIME_DIR` applied by meson from `tests/meson.build`)
- Bless a golden: `BLESS=1 meson test -C build blackboxai:<name>`
- Full gate before each feature's final commit: `WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build` then `gcovr -r . build --filter 'toolkit/' --filter 'src/' --fail-under-line=80`

---

# Feature 1 — Active/inactive frame-focus swap

Threads a `focused` boolean from the seat layer into `Decoration::rebuild`; no new wlroots calls (reuses the existing rebuild/destroy path). Lands first so F4 inherits a factored focus hook.

### Task F1.1: Decoration gains focused/unfocused palette + a pure selection helper

**Files:**
- Modify: `src/Decoration.cc:12-35` (palette), `src/Decoration.hh:35` (signature), add a testable helper.
- Create: `tests/unit/decoration_palette_test.cc`
- Modify: `tests/meson.build` (register the unit test)

- [ ] **Step 1 — Write the failing test.** `tests/unit/decoration_palette_test.cc` (no wlroots needed; include only the palette header bits):

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "DecorationPalette.hh"   // new tiny header carrying the Look pairs + selector

using namespace bbai::deco;

TEST_CASE("active and inactive looks differ but active == M3 constants") {
  // Active stays byte-identical to the M3 values.
  CHECK(std::string(lookFor(Element::Title, true).c1)  == "#c0c0c0");
  CHECK(std::string(lookFor(Element::Label, true).c1)  == "#b8b8b8");
  // Inactive is distinct.
  CHECK(std::string(lookFor(Element::Title, false).c1) != std::string(lookFor(Element::Title, true).c1));
  CHECK(std::string(lookFor(Element::Label, false).c1) != std::string(lookFor(Element::Label, true).c1));
  CHECK(textColorFor(false).red()   != textColorFor(true).red());   // greyer inactive text
  CHECK(borderColorFor(false).red() != borderColorFor(true).red());
}
```

- [ ] **Step 2 — Run, expect FAIL** (header/symbols absent): `meson test -C build blackboxai:decoration_palette` → FAIL to compile.

- [ ] **Step 3 — Implement.** Extract the anonymous-namespace palette from `Decoration.cc:12-35` into `src/DecorationPalette.hh` as focused/unfocused pairs behind a selector. Keep the `focused=true` values byte-identical to the current `kTitleLook`/etc.:

```cpp
// src/DecorationPalette.hh
#ifndef BLACKBOXAI_DECORATION_PALETTE_HH
#define BLACKBOXAI_DECORATION_PALETTE_HH
#include "Color.hh"
#include <string>
namespace bbai::deco {
  struct Look { const char *desc; const char *c1; const char *c2; };
  enum class Element { Title, Label, Handle, Grip, Button };
  // Active = exact M3 constants; inactive = Gray-style desaturated.
  inline Look lookFor(Element e, bool f) {
    switch (e) {
      case Element::Title:  return f ? Look{"raised gradient diagonal","#c0c0c0","#808080"}
                                     : Look{"raised gradient diagonal","#909090","#606060"};
      case Element::Label:  return f ? Look{"sunken gradient diagonal","#b8b8b8","#888888"}
                                     : Look{"sunken gradient diagonal","#909090","#686868"};
      case Element::Handle: return f ? Look{"raised gradient diagonal","#c0c0c0","#808080"}
                                     : Look{"raised gradient diagonal","#909090","#606060"};
      case Element::Grip:   return f ? Look{"raised gradient diagonal","#d8d8d8","#909090"}
                                     : Look{"raised gradient diagonal","#a0a0a0","#707070"};
      case Element::Button: return f ? Look{"raised gradient diagonal","#e0e0e0","#a8a8a8"}
                                     : Look{"raised gradient diagonal","#a8a8a8","#808080"};
    }
    return {"",""," "};
  }
  inline bt::Color textColorFor(bool f)   { return f ? bt::Color(0,0,0)    : bt::Color(96,96,96); }
  inline bt::Color borderColorFor(bool f) { return f ? bt::Color(48,48,48) : bt::Color(72,72,72); }
  inline bt::Color picColorFor(bool f)    { return f ? bt::Color(32,32,32) : bt::Color(96,96,96); }
}
#endif
```

  Then in `Decoration.cc`: `#include "DecorationPalette.hh"`, delete the local `Look`/`k*Look`/`textColor`/`borderColor`/`picColor`, and make `makeTexture`/`renderTexture` take `deco::Look`. Add `bool focused` param to `rebuild` (default removed — all callers pass it; see F1.2). Register the test in `tests/meson.build` under the unit suite.

- [ ] **Step 4 — Run, expect PASS**: `meson test -C build blackboxai:decoration_palette` → PASS. Also `ninja -C build` clean.

- [ ] **Step 5 — Commit** (hila-voice): subject ~ `Decoration: split palette into focused/unfocused behind a pure selector`.

### Task F1.2: View carries focus state and re-lays-out on change

**Files:** Modify `src/View.hh:58-77`, `src/View.cc` (`relayout` ~61-74). Test: extend `tests/system/client_test.cc` or a small new nonvisual case.

- [ ] **Step 1 — Failing test** (headless-nonvisual): map one client, assert `isFocused()==false`; `setFocused(true)` → `isFocused()==true` and `sceneTree()` decorations still drawn; `setFocused(true)` again is idempotent (no extra rebuild — assert via a rebuild counter test seam or that `drawsFrame()` unchanged). Cover CSD-skip (a CSD view ignores `setFocused`).

```cpp
TEST_CASE("View tracks focus and re-lays-out on change") {
  // ... boot Server headless, map one SSD TestClient, pump until mapped ...
  View *v = server.viewsForTest()[0].get();
  CHECK(v->isFocused() == false);
  v->setFocused(true);
  CHECK(v->isFocused() == true);
  v->setFocused(true);                 // idempotent
  CHECK(v->isFocused() == true);
}
```

- [ ] **Step 2 — Run, expect FAIL** (`isFocused`/`setFocused` undefined).
- [ ] **Step 3 — Implement.** In `View.hh` add to the private block and public API:

```cpp
public:
  void setFocused(bool f);
  bool isFocused() const { return focused_; }
private:
  bool focused_ = false;
```

  `View.cc`:

```cpp
void View::setFocused(bool f) {
  if (focused_ == f) return;
  focused_ = f;
  if (mapped && draw_frame) relayout();
}
```

  Update `relayout()` to pass focus: `deco->rebuild(cw, ch, title, focused_);` (title source unchanged). `new_xdg_toplevel` stays as-is (no auto-focus — see decisions).

- [ ] **Step 4 — Run, expect PASS.**
- [ ] **Step 5 — Commit:** `View: carry focus state, re-lay-out the frame on change`.

### Task F1.3: focusView/clearFocus drive the restyle; frame_test focuses so m3-frame-ssd stays active

**Files:** Modify `src/Server.cc` `focusView` (287-295) + `clearFocus` (~506-510). Modify `tests/system/frame_test.cc` (add focus click). Test: two-client nonvisual in `restack_test.cc` pattern or a new case.

- [ ] **Step 1 — Failing test** (two clients): map red then green; drive focus to red via `injectPointerButtonForTest` over red's titlebar; `CHECK(red->isFocused() && !green->isFocused())`; focus green; assert the flip; `focusedViewForTest()` agrees.
- [ ] **Step 2 — Run, expect FAIL** (restyle not wired → both unfocused initially; the post-focus assertion that the OTHER view flipped to `!isFocused()` fails because `focusView` never calls `setFocused`).
- [ ] **Step 3 — Implement.** `focusView` (after the early-return / before/after the activate calls):

```cpp
void Server::focusView(View *v) {
  if (focused_view == v) return;
  if (focused_view) {
    wlr_xdg_toplevel_set_activated(focused_view->toplevel(), false);
    focused_view->setFocused(false);            // NEW
  }
  focused_view = v;
  wlr_xdg_toplevel_set_activated(v->toplevel(), true);
  v->setFocused(true);                          // NEW
  if (wlr_keyboard *kb = wlr_seat_get_keyboard(seat))
    wlr_seat_keyboard_notify_enter(seat, v->toplevel()->base->surface,
                                   kb->keycodes, kb->num_keycodes, &kb->modifiers);
}
```

  `clearFocus` (before nulling `focused_view`): `if (focused_view) focused_view->setFocused(false);`. `removeView` already routes focus changes through these, so it is covered.
  In `tests/system/frame_test.cc`, before `captureFrame` (line ~52), focus the window so the canonical reference stays the *active* look:

```cpp
server.injectPointerMotionForTest(260, 130);          // over the titlebar
server.injectPointerButtonForTest(BTN_LEFT, true);
server.injectPointerButtonForTest(BTN_LEFT, false);   // click w/o motion = focus, no move
```

- [ ] **Step 4 — Run, expect PASS** for the new test AND `meson test -C build blackboxai:frame` still PASSES against the unchanged `m3-frame-ssd.png` (active look, now via real focus).
- [ ] **Step 5 — Commit:** `Server: restyle decorations in focusView/clearFocus; frame_test now focuses`.

### Task F1.4: Two-window focused/unfocused golden

**Files:** Create `tests/golden/m4-focus-swap.png`; test in a new case (register under `text_env`).

- [ ] **Step 1 — Failing test** (headless golden, `text_env`): map two overlapping SSD clients offset so both titlebars are visible; focus exactly one; `compareGolden(frame, "tests/golden/m4-focus-swap.png", 2, /*budget*/40)`. Spot-CHECK: focused titlebar pixel == active `#c0c0c0`-derived value; unfocused titlebar pixel == inactive `#909090`-derived; unfocused label region greyer.
- [ ] **Step 2 — Run, expect FAIL** (golden absent → harness writes `-actual.png`).
- [ ] **Step 3 — Implement.** No new production code; verify the inactive hex from F1.1 renders a clearly distinct look. First confirm `m3-frame-ssd` unchanged: `meson test -C build blackboxai:frame` PASS.
- [ ] **Step 4 — Bless + verify:** `BLESS=1 meson test -C build blackboxai:focus_swap` then `meson test -C build blackboxai:focus_swap` → PASS. Eyeball `tests/golden/m4-focus-swap.png`.
- [ ] **Step 5 — Feature gate + commit.** Full suite + coverage gate (commands above). Commit: `Golden: two-window active/inactive focus swap`.

---

# Feature 3 — Cascade submenus (Workspaces submenu reaches New/RemoveWorkspace)

Builds on the existing `Kind::Submenu` scaffold. Collapses the four inline workspace rows into one `Workspaces` submenu and makes `NewWorkspace`/`RemoveWorkspace` reachable. Adds the root-menu keybinding.

### Task F3.1: MenuItem carries child items; Rootmenu emits a Workspaces submenu

**Files:** Modify `src/MenuItem.hh` (add `submenu_items`, drop `Menu *sub`), `src/Rootmenu.cc:29-46`. Test: unit (no Server).

- [ ] **Step 1 — Failing test** (`tests/unit`, calls `rootmenu::build`):

```cpp
TEST_CASE("root menu collapses workspaces into one submenu") {
  bbai::WorkspaceModel ws;                       // default 4 workspaces, current 0
  auto items = bbai::rootmenu::build(ws);
  // top level: foot, xterm, sep, Workspaces(submenu), sep, Restart, Exit
  REQUIRE(items.size() == 7);
  CHECK(items[3].kind == bbai::MenuItem::Kind::Submenu);
  CHECK(items[3].label == bt::decodeUtf8("Workspaces"));
  // no inline WorkspaceSwitch rows at top level
  for (auto &it : items) CHECK(it.action != bbai::MenuItem::Act::WorkspaceSwitch);
  auto &sub = items[3].submenu_items;
  CHECK(sub.size() == ws.count() + 3);           // N switch rows + sep + New + Remove
  CHECK(sub[ws.current()].checked == true);
  CHECK(sub.back().action == bbai::MenuItem::Act::RemoveWorkspace);
}
```

- [ ] **Step 2 — Run, expect FAIL** (`submenu_items` absent; build still flat).
- [ ] **Step 3 — Implement.** `MenuItem.hh`: replace `Menu *sub = nullptr;` with `std::vector<MenuItem> submenu_items;`. `Rootmenu.cc`: add a builder and swap the inline loop (29-46):

```cpp
static std::vector<MenuItem> buildWorkspacesSubmenu(const WorkspaceModel &ws) {
  std::vector<MenuItem> sub;
  for (unsigned i = 0; i < ws.count(); ++i) {
    MenuItem m; m.label = bt::decodeUtf8(ws.name(i).c_str());
    m.action = MenuItem::Act::WorkspaceSwitch; m.workspace = i;
    m.checked = (i == ws.current());
    sub.push_back(std::move(m));
  }
  sub.push_back(separator());
  sub.push_back(simple("New Workspace", MenuItem::Act::NewWorkspace));
  sub.push_back(simple("Remove Last Workspace", MenuItem::Act::RemoveWorkspace));
  return sub;
}
// in build(): replace the inline `for ws` loop with:
MenuItem wsm; wsm.kind = MenuItem::Kind::Submenu;
wsm.label = bt::decodeUtf8("Workspaces");
wsm.submenu_items = buildWorkspacesSubmenu(ws);
items.push_back(std::move(wsm));
```

- [ ] **Step 4 — Run, expect PASS.**
- [ ] **Step 5 — Commit:** `Rootmenu: collapse workspaces into a submenu (New/Remove reachable)`.

### Task F3.2: Menu draws a submenu arrow; re-bless the (shorter) root-menu goldens

**Files:** Modify `src/Menu.cc` (`emitItem` ~101-127, add `drawArrow`). Re-bless `m4-rootmenu.png`, `m4-rootmenu-hilite.png`.

- [ ] **Step 1 — Failing test** (headless golden): open the root menu (right-click desktop) and capture; the `Workspaces` row shows a right-arrow glyph in the right gutter; the menu is shorter (one row replaces four). Compare to the existing `m4-rootmenu.png` → FAIL (content changed).
- [ ] **Step 2 — Run, expect FAIL** (mismatch → `-actual`/`-diff` written).
- [ ] **Step 3 — Implement.** Add a gutter triangle mirroring `drawCheck` (`Menu.cc:40-44`):

```cpp
void drawArrow(std::vector<uint32_t> &px, int w, int h, const bt::Color &c) {
  const int x0 = w - 9, cy = h / 2;            // right gutter
  for (int d = 0; d <= 4; ++d)
    for (int dy = -(4 - d); dy <= (4 - d); ++dy) setPx(px, w, h, x0 + d, cy + dy, c);
}
```

  In `emitItem`, after drawing text, `if (it.kind == MenuItem::Kind::Submenu) drawArrow(px, r.w, r.h, tc);`. `computeLayout` already produces fewer rows so height/width shrink automatically.
- [ ] **Step 4 — Bless + verify:** `BLESS=1 meson test -C build blackboxai:rootmenu` then `meson test -C build blackboxai:rootmenu` → PASS. Eyeball both goldens (fewer rows + arrow on Workspaces).
- [ ] **Step 5 — Commit:** `Menu: submenu arrow indicator; re-bless shorter root menu`.

### Task F3.3: Hovering/pressing a submenu row opens the child to the right

**Files:** Modify `src/Menu.hh`/`src/Menu.cc` (add `parent_`/`child_`, `openSubmenu`/`closeSubmenu`, introspection), `src/Server.cc` `onPointerMotion` modal-hover branch (~337-340). Create `tests/golden/m4-rootmenu-submenu.png`.

- [ ] **Step 1 — Failing test** (golden + nonvisual): open root menu at known origin; move pointer onto the `Workspaces` row; assert `menuOpenForTest()` still true AND new `submenuOpenForTest()`/`submenuItemCountForTest()` report the child open with `ws.count()+3` items; assert child X == parent right edge and child Y ≈ parent row top. Bless `m4-rootmenu-submenu.png` (root + open child).
- [ ] **Step 2 — Run, expect FAIL** (accessors/opening absent).
- [ ] **Step 3 — Implement.** `Menu.hh`: add `Menu *parent_ = nullptr; std::unique_ptr<Menu> child_; int open_sub_ = -1;` and:

```cpp
void openSubmenuAt(int index);   // lazily build child_ from items_[index].submenu_items
void closeSubmenu();             // child_.reset(); open_sub_ = -1;
bool submenuOpenForTest() const { return (bool)child_; }
int submenuItemCountForTest() const { return child_ ? child_->itemCount() : 0; }
Menu *liveChildForTest() const { return child_.get(); }
```

  `openSubmenuAt(i)` builds `child_ = std::make_unique<Menu>(server_, items_[i].label, items_[i].submenu_items)`, sets `child_->parent_ = this`, and `child_->show(gx_ + layout_.width, gy_ + layout_.items[i].y)` (Menu::show already clamps on-screen → subsumes the edge-flip). `Server::onPointerMotion` modal-hover branch: if the hovered row is `Kind::Submenu`, `active_menu_->openSubmenuAt(row)`; if a different row, `active_menu_->closeSubmenu()`.
- [ ] **Step 4 — Bless + verify:** `BLESS=1 meson test -C build blackboxai:rootmenu_submenu` then PASS.
- [ ] **Step 5 — Commit:** `Menu: open a child submenu to the right on hover`.

### Task F3.4: Navigation + leaf actions; collapse the hardcoded test indices

**Files:** Modify `src/Server.cc` `liveMenu()`/`handleMenuKey` (~619-647)/`handleMenuButton` (~609-617)/`itemClicked` (~649-664) + the obsolete comment (656-657). Update `tests/system/menu_action_test.cc:71`, `tests/system/rootmenu_test.cc:78,261-265`.

- [ ] **Step 1 — Failing test** (nonvisual): open menu, navigate to `Workspaces`, Return opens the child (menu still open, active index on the child); in the child, Return on `New Workspace` → `workspaces().count()` grows; `Remove Last Workspace` → shrinks but never below 1; a click on a child `WorkspaceSwitch` row switches and dismisses the whole chain. Update the existing index asserts to the collapsed layout (Restart/Exit now 5/6).
- [ ] **Step 2 — Run, expect FAIL** (dispatch targets the root, not the child; old indices wrong).
- [ ] **Step 3 — Implement.** Add `Menu *Server::liveMenu()` returning the deepest open child (`for (Menu *m = active_menu_; ; ) { if (m->liveChildForTest()) m = m->liveChildForTest(); else return m; }`). Route `handleMenuKey`/`handleMenuButton`/`itemClicked` dispatch against `liveMenu()`. Return on a `Submenu` row opens its child and keeps the menu open; a leaf action calls `closeMenus()` then dispatches (the existing `switch` in `itemClicked` already handles `NewWorkspace`/`RemoveWorkspace`/`WorkspaceSwitch`). Update the obsolete comment at 656-657. Fix `menu_action_test.cc:71` and `rootmenu_test.cc:78,261-265` to the new indices (Restart=5, Exit=6).
- [ ] **Step 4 — Run, expect PASS** (`menu_action`, `rootmenu`, and the new nav test).
- [ ] **Step 5 — Commit:** `Server: route menu nav/clicks through the deepest open submenu`.

### Task F3.5: Dismissal closes the whole chain; root-menu keybinding

**Files:** Modify `src/Server.cc` (`closeMenus`, `handleMenuButton` outside-click, Escape), `src/Keybindings.hh` (add `Mod4+Space` → open root menu). Test: nonvisual.

- [ ] **Step 1 — Failing test:** with the child open, a left-press outside both rects closes everything (`menuOpenForTest()` false); Escape from the child closes the whole chain; a press inside the parent on a plain row closes the stale child but keeps the parent open. Separately: pressing `Mod4+Space` (via `injectKeyForTest`) with no menu open → root menu opens.
- [ ] **Step 2 — Run, expect FAIL.**
- [ ] **Step 3 — Implement.** `closeMenus()` resets `active_menu_` (its dtor destroys `child_` transitively); ensure `liveMenu()` is never called after reset. `handleMenuButton`: outside the deepest rect AND outside the parent → `closeMenus()`; inside parent on a plain row → `closeSubmenu()`. Escape on the deepest → `closeMenus()`. In `Keybindings.hh` add a `Mod4+Space` entry mapped to a new action `Act::RootMenu`; `executeAction(RootMenu)` opens the root menu centered (reuse the right-click open path, position at output center).
- [ ] **Step 4 — Run, expect PASS.**
- [ ] **Step 5 — Feature gate + commit.** Full suite + gate. Commit: `Server: chain dismissal + Mod4+Space opens the root menu`.

---

# Feature 4 — Iconbar + iconify/maximize wiring

Wires the drawn-but-dead window buttons, adds iconified/maximized View state, an Iconified-Windows menu (middle-click + `Mod4+Alt+T`), and the toolbar window-label-on-focus.

### Task F4.1: Split Part::Button into per-button parts

**Files:** Modify `src/Decoration.hh:22` (enum), `src/Server.cc` `partAt` (281-282) + dead press branch (~397). Test: extend `tests/system/hittest_test.cc:47-48`.

- [ ] **Step 1 — Failing test:** the iconify rect resolves to `Part::IconifyButton`, maximize → `Part::MaximizeButton`, close → `Part::CloseButton`; client/titlebar/grip parts unchanged.
- [ ] **Step 2 — Run, expect FAIL** (only `Part::Button` exists).
- [ ] **Step 3 — Implement.** `enum class Part { None, Titlebar, Label, IconifyButton, MaximizeButton, CloseButton, LeftGrip, RightGrip, Client };` Rewrite `partAt` 281-282:

```cpp
if (in(iconifyButton(W, H)))  return Part::IconifyButton;
if (in(maximizeButton(W, H))) return Part::MaximizeButton;
if (in(closeButton(W, H)))    return Part::CloseButton;
```

  Update the dead press branch (~397) to swallow on any of the three button parts (no behavior change yet).
- [ ] **Step 4 — Run, expect PASS** (`hittest`).
- [ ] **Step 5 — Commit:** `Decoration/Server: per-button frame parts in the hit-test`.

### Task F4.2: View gains iconified state + composed visibility

**Files:** Modify `src/View.hh`, `src/View.cc` (`setOnWorkspace`/`visible` ~82-86). Test: nonvisual.

- [ ] **Step 1 — Failing test:** map a client, `server.iconifyForTest(v)`; assert `isIconified()`, `visible()==false`, and a workspace round-trip keeps it hidden.
- [ ] **Step 2 — Run, expect FAIL.**
- [ ] **Step 3 — Implement.** Add `bool iconified_ = false; bool isIconified() const; void setIconified(bool);`. Refactor `setOnWorkspace`/`visible` into a private `applyVisibility()` that enables the frame only when `on_ws && !iconified_`; `setIconified(bool)` toggles + calls `applyVisibility()`. Add a `Server::iconifyForTest` lever.
- [ ] **Step 4 — Run, expect PASS.**
- [ ] **Step 5 — Commit:** `View: iconified state composes with workspace visibility`.

### Task F4.3: Iconify-button release iconifies + re-homes focus (release-inside semantics)

**Files:** Modify `src/Server.hh`/`src/Server.cc` (`onPointerButton` 369-403; add `iconifyView`, `rehomeFocusAfterLoss`, `pressed_button_part_`/`pressed_button_view_`, `icons_`). Test: nonvisual two-client.

- [ ] **Step 1 — Failing test:** map two SSD clients on ws0, focus A, press+release A's iconify button with the cursor staying inside it → A hidden, focus moved to the topmost survivor; a release that drifts off the button does NOT iconify.
- [ ] **Step 2 — Run, expect FAIL.**
- [ ] **Step 3 — Implement.** Factor `rehomeFocusAfterLoss(View*)` from `removeView`'s 227-230 logic (topmost-else-clear) and route both through it. In `onPointerButton`: on press over a button part, record `pressed_button_part_`/`pressed_button_view_` and swallow; on release, if same view+button still under the cursor (`partAt` matches), dispatch — iconify path = `iconifyView(v)` (`v->setIconified(true)`, push to `icons_`, `rehomeFocusAfterLoss(v)`). Clear pressed state on every release. Keep button press-state orthogonal to `cursor_mode` so move/resize release (373-380) is not regressed.
- [ ] **Step 4 — Run, expect PASS.**
- [ ] **Step 5 — Commit:** `Server: iconify button iconifies the window and re-homes focus`.

### Task F4.4: Close button release closes the window

**Files:** Modify `src/Server.cc` (release dispatcher). Test: nonvisual.

- [ ] **Step 1 — Failing test:** press+release the close button inside bounds → `TestClient.gotCloseRequest()` true; press then release outside → no close.
- [ ] **Step 2 — Run, expect FAIL.**
- [ ] **Step 3 — Implement.** Close branch of the release dispatcher calls `wlr_xdg_toplevel_send_close(v->toplevel())` (same call `executeAction(CloseWindow)` uses at ~490).
- [ ] **Step 4 — Run, expect PASS.**
- [ ] **Step 5 — Commit:** `Server: close button sends xdg close on release-inside`.

### Task F4.5: Maximize button toggles maximize with geometry save/restore

**Files:** Modify `src/View.hh`/`src/View.cc` (`setMaximized`, premax quartet), `src/Server.cc` (release dispatcher passes the work-area). Test: nonvisual.

- [ ] **Step 1 — Failing test:** record A's x/y/cw/ch; press+release maximize → A occupies the work area (`x==0,y==0,w==output_w,h==output_h-kBarHeight`) and `isMaximized()`; again → exact original geometry restored and `!isMaximized()`.
- [ ] **Step 2 — Run, expect FAIL.**
- [ ] **Step 3 — Implement.** `View::setMaximized(bool, int areaW, int areaH)`: on enable save `premax_{x,y,w,h}`, `resizeTo(0,0,areaW,areaH)`, `wlr_xdg_toplevel_set_maximized(toplevel(), true)`; on disable `resizeTo(premax_*)` + `set_maximized(false)`. Server computes the target from `activeOutputSize` (~561-565) minus `toolbar::kBarHeight` and passes it into the release dispatcher.
- [ ] **Step 4 — Run, expect PASS.**
- [ ] **Step 5 — Commit:** `View: maximize toggles to work area with save/restore`.

### Task F4.6: Golden — a maximized window fills the work area above the toolbar

**Files:** Create `tests/golden/m4-maximized.png`. Test: headless golden under `text_env`.

- [ ] **Step 1 — Failing test:** map one red SSD client, fire maximize, pump (the existing 500+30 async ack/commit pattern), capture; assert the frame spans full width and stops at the toolbar top; toolbar+clock still render below. Compare to absent golden → FAIL.
- [ ] **Step 2 — Run, expect FAIL.**
- [ ] **Step 3 — Implement.** No new code; compose the existing capture path.
- [ ] **Step 4 — Bless + verify:** `BLESS=1 meson test -C build blackboxai:maximized` then PASS; eyeball.
- [ ] **Step 5 — Commit:** `Golden: maximized window fills the work area`.

### Task F4.7: Iconified-Windows menu (middle-click + Mod4+Alt+T) deiconifies on click

**Files:** Modify `src/MenuItem.hh` (`Act::Deiconify` + `void *target`), `src/Server.cc` (`buildIconMenu`, `itemClicked` Deiconify case, `deiconifyView`, drop from `icons_` in `removeView`, middle-click open in `onPointerButton`, `openIconMenuForTest`), `src/Keybindings.hh` (`Mod4+Alt+T`). Test: nonvisual.

- [ ] **Step 1 — Failing test:** iconify A, open the icon menu via `openIconMenuForTest()` (and assert `Mod4+Alt+T` via `injectKeyForTest` opens it too); assert `menuOpenForTest()` and one item per icon; click A's item → A visible again, focused, removed from `icons_`, menu dismissed. Cover `removeView`-while-iconified dropping the stale handle.
- [ ] **Step 2 — Run, expect FAIL.**
- [ ] **Step 3 — Implement.** `MenuItem.hh`: add `Deiconify` to `Act` and `void *target = nullptr;`. `buildIconMenu()` produces one `Deiconify` item per `icons_` entry (label = ellided title, `target = view`). `itemClicked` gains a `Deiconify` case resolving `target` via `viewForHandle` (~512) → `deiconifyView` (`setIconified(false)` + `raiseView` + `focusView`). Drop from `icons_` in `removeView` (~210). Middle-click on desktop background (the existing right-click-opens-root pattern, `BTN_MIDDLE`) opens `buildIconMenu()`; `Mod4+Alt+T` → `executeAction(IconMenu)` does the same. Add `openIconMenuForTest`.
- [ ] **Step 4 — Run, expect PASS.**
- [ ] **Step 5 — Commit:** `Server: Iconified-Windows menu (middle-click + Mod4+Alt+T) deiconifies`.

### Task F4.8: Toolbar window-label tracks the focused window

**Files:** Modify `src/Server.cc` `focusView`/`clearFocus`. Create `tests/golden/m4-toolbar-winlabel.png`. Test: golden + assert.

- [ ] **Step 1 — Failing test:** focus a client titled `bbai-test` → toolbar window-label shows it; iconify/close → label blanks. Bless `m4-toolbar-winlabel.png`. Keep the pre-existing `m4-toolbar`/`m4-ws*` tests focusing nothing so they stay blank-label and unchanged.
- [ ] **Step 2 — Run, expect FAIL** (`redrawWindowLabel` never called → label always blank).
- [ ] **Step 3 — Implement.** From `focusView` call `toolbar_->redrawWindowLabel(v->toplevel()->title)`; from `clearFocus` call `toolbar_->redrawWindowLabel(nullptr)`. (`Toolbar::redrawWindowLabel` already exists at `Toolbar.cc:92`.)
- [ ] **Step 4 — Bless + verify:** `BLESS=1 meson test -C build blackboxai:toolbar_winlabel`; confirm `meson test -C build blackboxai:toolbar` and `:workspace_switch` still PASS unchanged.
- [ ] **Step 5 — Feature gate + commit.** Full suite + gate. Commit: `Server: toolbar window-label follows focus`.

---

# Feature 2 — Toolbar auto-hide + placements

Lands last (heaviest `Toolbar` rewrite; its new goldens bless over already-final toolbar behavior). Default auto-hide OFF.

### Task F2.1: Placement enum + parameterized barRect

**Files:** Modify `src/Toolbar.geom.hh` (`barRect` 33-36). Test: extend `tests/unit/toolbar_geom_test.cc`.

- [ ] **Step 1 — Failing test:** `barRect(1280,720,BottomCenter) == {218,697,844,23}` (unchanged); add `TopLeft=={0,0,844,23}`, `TopRight=={436,0,844,23}`, `BottomLeft=={0,697,844,23}`, `BottomRight=={436,697,844,23}`, `TopCenter=={218,0,844,23}`; assert `sectionRects(844,...)` byte-identical regardless of placement.
- [ ] **Step 2 — Run, expect FAIL** (no `Placement`; `barRect` arity wrong).
- [ ] **Step 3 — Implement.** Add to `Toolbar.geom.hh`:

```cpp
enum class Placement { TopLeft, TopCenter, TopRight, BottomLeft, BottomCenter, BottomRight };
inline Rect barRect(int OW, int OH, Placement p = Placement::BottomCenter) {
  const int bw = barWidth(OW);
  int x = (OW - bw) / 2;
  if (p == Placement::TopLeft || p == Placement::BottomLeft) x = 0;
  else if (p == Placement::TopRight || p == Placement::BottomRight) x = OW - bw;
  const bool top = (p == Placement::TopLeft || p == Placement::TopCenter || p == Placement::TopRight);
  const int y = top ? 0 : OH - kBarHeight;
  return { x, y, bw, kBarHeight };
}
```

  Default arg keeps all existing callers pinned (no `.cc` change yet).
- [ ] **Step 4 — Run, expect PASS** (`toolbar_geom`).
- [ ] **Step 5 — Commit:** `Toolbar.geom: placement-parameterized barRect`.

### Task F2.2: Toolbar threads a placement through rebuild

**Files:** Modify `src/Toolbar.hh`/`src/Toolbar.cc` (`rebuild` ~99, add `placement_`, `placementForTest`). Test: extend `tests/system/toolbar_test.cc`.

- [ ] **Step 1 — Failing test:** existing `toolbar` test still passes against `m4-toolbar.png` (BottomCenter default); new assert: `placementForTest()==BottomCenter` and `barRectForTest().y==697`.
- [ ] **Step 2 — Run, expect FAIL** (`placementForTest` absent).
- [ ] **Step 3 — Implement.** Add `toolbar::Placement placement_ = toolbar::Placement::BottomCenter;` to `Toolbar`. In `rebuild` (line 99) call `toolbar::barRect(ow_, oh_, placement_)`; `barRectForTest` likewise. Add `toolbar::Placement placementForTest() const { return placement_; }`.
- [ ] **Step 4 — Run, expect PASS** (pixels identical; BottomCenter).
- [ ] **Step 5 — Commit:** `Toolbar: thread placement through rebuild (default BottomCenter)`.

### Task F2.3: A non-default placement golden (TopCenter)

**Files:** Create `tests/system/toolbar_placement_test.cc`, `tests/golden/m4-toolbar-topcenter.png`. Modify `tests/meson.build` (register under `text_env`).

- [ ] **Step 1 — Failing test:** build Server, force toolbar to TopCenter via `setPlacementForTest(TopCenter)`, capture; assert grey chrome at y∈[0..22] and desktop gradient just below; compare to absent golden → FAIL.
- [ ] **Step 2 — Run, expect FAIL.**
- [ ] **Step 3 — Implement.** Add `void Toolbar::setPlacementForTest(toolbar::Placement p) { placement_ = p; rebuild(); }` (no production caller; placement source is M5 config).
- [ ] **Step 4 — Bless + verify:** `BLESS=1 meson test -C build blackboxai:toolbar_placement` then PASS; eyeball.
- [ ] **Step 5 — Commit:** `Toolbar: TopCenter placement golden`.

### Task F2.4: hiddenBarRect pure helper

**Files:** Modify `src/Toolbar.geom.hh`. Test: extend `tests/unit/toolbar_geom_test.cc`.

- [ ] **Step 1 — Failing test:** `hiddenBarRect(barRect(1280,720,BottomCenter), BottomCenter) == {218,718,844,23}` (slid down, 2px sliver); `hiddenBarRect(barRect(1280,720,TopCenter), TopCenter) == {218,-21,844,23}`; width/x/h preserved.
- [ ] **Step 2 — Run, expect FAIL.**
- [ ] **Step 3 — Implement.**

```cpp
inline Rect hiddenBarRect(Rect shown, Placement p) {
  const bool top = (p == Placement::TopLeft || p == Placement::TopCenter || p == Placement::TopRight);
  Rect r = shown;
  r.y = top ? shown.y + kHiddenHeight - shown.h    // slide up, leave kHiddenHeight sliver
            : shown.y + shown.h - kHiddenHeight;    // slide down
  return r;
}
```

- [ ] **Step 4 — Run, expect PASS.**
- [ ] **Step 5 — Commit:** `Toolbar.geom: hiddenBarRect slide math`.

### Task F2.5: Auto-hide state + a hide timer that slides the bar

**Files:** Modify `src/Toolbar.hh`/`src/Toolbar.cc` (add `auto_hide_`/`hidden_`, an inner `HideTick : TimeoutHandler`, `applyPosition`, `onPointerOverToolbar`, `onHideTimeout`). Test: headless position-only.

- [ ] **Step 1 — Failing test:** construct Toolbar with auto-hide on (test seam); `hiddenForTest()==true` at start and the tree position == `hiddenBarRect`; `onPointerOverToolbar(true)` + `advanceClockForTest` past `kHideDelayMs` → `hiddenForTest()==false`, node at shown `barRect`; then `onPointerOverToolbar(false)` + advance → hidden again. Position-only (no golden).
- [ ] **Step 2 — Run, expect FAIL.**
- [ ] **Step 3 — Implement.** The clock path must stay on `Toolbar::timeout` (Timer holds one handler), so add a **separate** handler for the hide timer:

```cpp
// in Toolbar (private):
struct HideTick : TimeoutHandler { Toolbar *tb; void timeout() override { tb->onHideTimeout(); } } hide_handler_{this};
std::unique_ptr<Timer> hide_timer_;
bool auto_hide_ = false, hidden_ = false;
static constexpr int kHideDelayMs = 250;
void applyPosition();          // set tree position to (hidden_ ? hiddenBarRect : barRect)
void onHideTimeout();          // flip hidden_, applyPosition()
void onPointerOverToolbar(bool over);   // start/stop hide_timer_ per reference enter/leave
```

  `hidden_` inits to `auto_hide_`; `rebuild()` ends with `applyPosition()`. `onPointerOverToolbar(true)` while hidden starts the (one-shot) reveal timer; `false` while shown starts the hide timer; idempotent across repeated calls. Add `hiddenForTest()`/a constructor/test seam to enable auto-hide. Build `hide_timer_` with `Timer(server_.timerRegistry(), hide_handler_)`.
- [ ] **Step 4 — Run, expect PASS.**
- [ ] **Step 5 — Commit:** `Toolbar: auto-hide state + hide timer slides the bar`.

### Task F2.6: Compositor wires pointer-over to the edge-trigger; hidden golden

**Files:** Modify `src/Server.cc` `onPointerMotion` non-modal branch (~357-366), add an auto-hide enable seam. Create `tests/golden/m4-toolbar-hidden.png`; reuse `m4-toolbar.png` for the shown state.

- [ ] **Step 1 — Failing test:** enable auto-hide; `injectPointerMotionForTest` into the visible sliver band, advance clock → slid in (`hiddenForTest` false), captured frame full grey chrome → compare to existing `m4-toolbar.png` (PASS). Move pointer away, advance → slid out → NEW `m4-toolbar-hidden.png` (2px sliver).
- [ ] **Step 2 — Run, expect FAIL** (no edge-trigger; hidden golden absent).
- [ ] **Step 3 — Implement.** In `onPointerMotion` non-modal branch (357-366): compute `over` from the pure `barRect`/`hiddenBarRect` using `cursor->x,y` and call `toolbar_->onPointerOverToolbar(over)` — no-op when `auto_hide_` off. Add a `Server` seam to enable auto-hide for the test. `onPointerOverToolbar` is idempotent across repeated motion.
- [ ] **Step 4 — Bless + verify:** `BLESS=1 meson test -C build blackboxai:toolbar_hidden` (hidden state) then PASS; the shown state reuses `m4-toolbar.png`.
- [ ] **Step 5 — Feature gate + commit.** Full suite + coverage gate. Commit: `Server: pointer-over edge-trigger reveals/hides the toolbar`.

---

## Self-Review

**Spec coverage** (`docs/superpowers/specs/2026-06-17-blackboxai-wlroots-0.20-port.md` §3 Phase 2 + the four feature bullets):
- Active/inactive focus swap → F1.1–F1.4. ✓
- Toolbar auto-hide + placements → F2.1–F2.6. ✓
- Cascade submenus (+ New/RemoveWorkspace reachable; no menu-file parsing) → F3.1–F3.5. ✓
- Iconbar + iconify/maximize → F4.1–F4.8. ✓
- Resolved open questions baked into "Decisions" (auto-focus off, auto-hide off, icon-menu open = middle-click + `Mod4+Alt+T`, root-menu `Mod4+Space`, inactive palette, delay, work-area, submenu clamp). ✓

**Placeholder scan:** no TBD/TODO; every code step shows code or an exact edit-site + snippet; every test step shows the assertion shape; every run step gives the command + expected result.

**Type/name consistency:** `Part::{IconifyButton,MaximizeButton,CloseButton}` (F4.1) used in F4.3–F4.5; `setFocused`/`isFocused` (F1.2) used in F1.3–F1.4 and inherited by F4; `submenu_items` (F3.1) used in F3.3; `toolbar::Placement`/`hiddenBarRect` (F2.1/F2.4) used in F2.2–F2.6; `liveMenu()`/`openSubmenuAt`/`closeSubmenu` (F3.3) used in F3.4–F3.5; `rehomeFocusAfterLoss` (F4.3) reused by `removeView`. Consistent.

**Ordering invariants:** F1 before F4 (shared `focusView`/`View`/`Decoration`); F3 before F4 (`MenuItem`/`itemClicked`, index renumber lands once); F4 before F2 (F2 rebases over the single `redrawWindowLabel` call site and blesses toolbar goldens over final behavior). Each feature ends on a full-suite + coverage-gate green before the next.
