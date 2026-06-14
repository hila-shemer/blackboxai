# BlackboxAI Milestone 3 — Blackbox decorations + interactive move/resize + title text — Plan

**Goal:** A real Wayland client receives a **pixel-accurate Blackbox server-side
decoration frame** — titlebar + window-title **text rendered via fcft** + bottom
handle + left/right resize grips + 1px border + three drawn buttons
(iconify/maximize/close). The window is interactively **movable** by dragging the
titlebar and **resizable** by dragging the grips. Decoration policy: **request
SSD, honor CSD holdouts**. All proven headlessly by golden-PNG + unit tests;
combined `toolkit/`+`src/` line coverage stays ≥80%; M1 + M2 goldens still pass.

**Builds on M2.** Reuses the renderer seam (`bt::Texture` → `bt::Image::renderBuffer`
→ `DataBuffer` → `wlr_scene_buffer`, exactly as `Output::renderBackground()`),
`Server`/`Output`, the headless capture fixture, the in-process `TestClient`, and
the `toolkit/wlr.hpp` C-interop boundary. Adds: an owned per-View frame scene
tree with decorations; the xdg-decoration protocol + SSD/CSD policy; seat/cursor/
input plumbing with a test-injection API; the interactive move/resize grab state
machine; and a new `toolkit/text.hpp` fcft boundary with a `bt::TextRenderer`.

This plan was grounded by a design-research pass (5 agents) that verified every
signature below against the **installed** `wlroots 0.19.3` / `fcft 3.3.3` headers
and the live M1/M2 source. Source-of-truth for frame geometry: the vendored
`reference/blackboxwm` (gitignored, untracked) `src/Window.cc` /
`src/ScreenResource.cc` and `data/styles/Rampage`.

---

## Decisions locked this session

- **Full fcft text now** (titlebar shows the real window title). Determinism via a
  bundled font + isolated fontconfig (below), not by excluding text from goldens.
- **Request SSD via `xdg-decoration-unstable-v1`, honor CSD holdouts** — default to
  server-side (we draw the frame); a client insisting on client-side gets
  `CLIENT_SIDE`, **no chrome**, geometry still managed.
- **Buttons: draw all three; wire `close` (always) + `maximize` (if the resize path
  lands cleanly); `iconify` draw-only** (minimized state is M4). Press feedback
  (pressed texture) is fine for all; iconify release is a no-op until M4.
- **New `toolkit/text.hpp` boundary** for fcft/pixman — NOT folded into `wlr.hpp`
  (independent sanitize need + pixman/harfbuzz transitive deps; keep each boundary
  self-contained).
- **Generalize the sanitize script**, not a sibling: `tools/sanitize-wlr-scene.sh`
  → strip `[static <ident>]` (not just `[static <digits>]`) **and** `restrict`. The
  broadened class is a strict superset, so it still fixes `wlr_scene.h`'s `[static 4]`.

---

## Architecture decisions

- **The keystone change: `View` owns a frame scene tree.** `View` stops calling
  `wlr_scene_xdg_surface_create(server.layer_window, …)` directly (current
  `View.cc:9`). Instead it owns `wlr_scene_tree *frame_tree =
  wlr_scene_tree_create(server.layer_window)` with `frame_tree->node.data = this`,
  parents the xdg-surface subtree **inside** it (`wlr_scene_xdg_surface_create(
  frame_tree, tl->base)` offset to the content origin), and parents one
  `wlr_scene_buffer` per decoration element. Hit-testing maps a hit scene node back
  to the View by walking parents to the tagged `node.data`. All four research docs
  assume this exact shape — sequence it first among the View edits.
- **Every decoration element goes through the existing renderer seam unchanged.**
  Build a `bt::Texture` (description string + colors), `bt::Image(w,h).renderBuffer(t)`
  → `std::vector<uint32_t>` ARGB8888 (opaque, alpha 0xFF), then for the label buffer
  blend glyphs in with `bt::TextRenderer::drawText` **between** `renderBuffer` and
  `DataBuffer::create`, then `wlr_scene_buffer_create` into `frame_tree`,
  `wlr_buffer_drop`, `wlr_scene_node_set_position`.
- **Decoration mode is chosen inside the existing `initial_commit` handler** so a
  single atomic `xdg_surface.configure` carries both size and decoration mode
  (`set_mode` + `set_size` only *schedule* into one pending configure). Minimal
  extension of the M2 commit handler, not a rewrite.
- **Input funnels through one pair of handlers.** Real `wlr_cursor->events`
  lambdas and the test-injection API both call the same
  `Server::onPointerMotion(time)` / `onPointerButton(time,btn,state)`, which run
  `wlr_scene_node_at` → parent-walk → part classification → grab state machine. So
  move/resize tests drive the *identical* code path the real backend would.
- **Test input injection over virtual devices.** The headless backend creates **no**
  input devices, so we expose `Server::injectPointerMotionForTest(lx,ly)` (=
  `wlr_cursor_warp(cursor,nullptr,lx,ly)` + `onPointerMotion(now)`) and
  `injectPointerButtonForTest(btn,state)` that funnel into the same handlers —
  deterministic, GPU-free, no real device. (Chosen over `wlr_virtual_pointer_v1`,
  which is a client protocol and awkward in-process.)
- **Seat advertises `POINTER|KEYBOARD` unconditionally** at `Server` construction
  (harmless under headless), independent of `new_input`, so the focus/activation
  path and any future client-pointer assertion work.

---

## Frame geometry (the numbers — pinned M3 defaults)

Blackbox derives metrics from the title font + style; we control the font, so each
is pinned. Title font `textHeight = 15`; element textures default to `bevel1`
(borderWidth 0 ⇒ all `BW_* = 0`):

```
button_width  = 7 + (0 + button_margin=2)*2 = 11  → max(11, label_height)
label_height  = max(15 + (0+2)*2, 11)       = 19
button_width  = max(11, 19)                 = 19   (buttons 19×19, square == label height)
title_height  = 19 + (0 + title_margin=2)*2 = 23   (titlebar 23 px)
grip_width    = button_width * 2            = 38   (each grip 38 px wide)
handle_height = window.handleHeight=6 + 0   = 6
frame_border_width (bw)                     = 1
```

**Layout math** (offsets relative to the frame-tree origin = frame top-left; the
frame tree is positioned at the View's `pos_x/pos_y`):

```
margin = { left:bw, right:bw, top:title_height, bottom:handle_height } = {1,1,23,6}
frame_w = W + 2*bw                         frame_h = H + title_height + handle_height
title   = (0, 0, frame_w, 23)             client  = (bw, title_height) = (1, 23)   size W×H
handle  = (0, 23+H, frame_w, 6)
left_grip  = (0,            23+H, 38, 6)   right_grip = (frame_w-38, 23+H, 38, 6)
by = title_margin + BW_title = 2          bwid = button_width + title_margin = 21
iconify = (by, by, 19, 19) = (2,2,19,19)  lx = by + bwid = 23 ; lw = frame_w - by - bwid
bx = frame_w - bwid                        (close first, rightmost; maximize marches left)
close    = (bx, by, 19,19) ; bx-=bwid ; lw-=bwid
maximize = (bx, by, 19,19) ; bx-=bwid ; lw-=bwid
label    = (lx, by, lw-by, 19)             (hidden if lw ≤ by)
border lines: 1px ring around (0,0,frame_w,frame_h) + under titlebar (y≈23) + above handle (y≈23+H)
```

**Worked example — content 200×150** (the M2 client): `frame = 202×179`
(`200+2 × 150+23+6`); titlebar `(0,0,202,23)`; client subtree origin `(1,23)`;
handle `(0,173,202,6)`; grips `(0,173,38,6)` and `(164,173,38,6)`. M2's golden
will therefore **change** (the red client gains a frame) — re-blessed in T11.

**Per-element textures** (canonical default from `data/styles/Rampage`; no-style
boot uses the literal `ScreenResource::load` fallbacks = every texture `flat solid`,
focus `white` / unfocus `black`, so the frame always renders):

| Element | focus description / color1 / color2 | unfocus |
|---|---|---|
| window.title | `sunken diagonal gradient bevel1` `#cccccc`→`#bbbbbb` | `sunken` `#707070` |
| window.label | `flat pyramid interlaced gradient` `#cccccc`→`#bbbbbb`, text `black` | `flat` `#707070`, text `black` |
| window.handle | `flat diagonal gradient bevel1` `#cccccc`→`#008888` | `raised pyramid gradient bevel1` |
| window.grip | `flat diagonal interlaced gradient bevel1` `grey90`→`grey40` | `flat diagonal gradient bevel1` `grey40`→`grey90` |
| window.button | `sunken diagonal gradient bevel1` `grey90`→`grey40`, pic `grey20` | `flat` `#707070`, pic `grey70` |
| window.button.pressed | `sunken interlaced diagonal gradient bevel1` `#380404`→`#0d0f0f` | (same) |
| window.frame border | solid 1px line `grey72` | `grey72` |

There is **no** `bevelWidth`/`handleWidth`/`frameWidth` resource in the window
namespace; the 3-D effect is each texture's own `bevel1` flag. Grips resize **only
the bottom corners** (`left_grip` = bottom-LEFT, `right_grip` = bottom-RIGHT); top
edge is fixed.

---

## Verified APIs (against installed headers)

**Input / seat / cursor** (`wlr_seat.h`, `wlr_cursor.h`, `wlr_pointer.h`, `wlr/util/edges.h`):
- `wlr_seat_create(display, name)`; `wlr_seat_set_capabilities(seat, WL_SEAT_CAPABILITY_*)`
  (the `wayland-server-protocol.h` bitmask, not a wlroots enum).
- Pointer focus (use `notify_*`, which respect grabs): `wlr_seat_pointer_notify_enter(
  seat, surface, sx, sy)`, `…_notify_motion(seat, time, sx, sy)`,
  `…_notify_button(seat, time, button, enum wl_pointer_button_state) → serial`,
  `…_notify_clear_focus(seat)`. One focused pointer surface at a time.
- `wlr_cursor_create()`, `wlr_cursor_attach_output_layout(cur, layout)`,
  `wlr_cursor_warp(cur, dev, lx, ly)`. Events on `cur->events`: `motion`
  (`wlr_pointer_motion_event`), `motion_absolute` (`…_motion_absolute_event`),
  `button` (`wlr_pointer_button_event{button(BTN_* linux code), state}`), `axis`,
  `frame` (no data). `button_event.state` is `enum wl_pointer_button_state`,
  pass-through to `notify_button`.
- `wlr_scene_node_at(&scene->tree.node, lx, ly, &sx, &sy) → wlr_scene_node*` (NULL if
  nothing); map the hit node to our View by walking `node->parent` to the tagged
  `frame_tree->node.data`. Classify part (titlebar/grip/button/client) by which
  element buffer / the xdg subtree was hit, or by geometry within the frame.
- Grab state machine (tinywl-style): `CursorMode {Passthrough, Move, Resize}`,
  grabbed View, grab geometry, resize `edges` (`WLR_EDGE_BOTTOM | LEFT|RIGHT`).
  Button-press on titlebar → begin Move; on grip → begin Resize(edges); motion
  updates position/size; button-release ends the grab and is **swallowed** (not
  forwarded). Also honor `tl->events.request_move`/`request_resize` after
  `wlr_seat_validate_pointer_grab_serial`.

**xdg-decoration** (`wlr_xdg_decoration_v1.h`):
- `wlr_xdg_decoration_manager_v1_create(display)`; event
  `manager->events.new_toplevel_decoration` carries `wlr_xdg_toplevel_decoration_v1*`.
- Struct fields: `requested_mode` (client ask), `pending`/`current` mode (acked),
  `toplevel` back-pointer; events `request_mode`, `destroy`. Mode enum
  `wlr_xdg_toplevel_decoration_v1_mode` = `NONE/CLIENT_SIDE/SERVER_SIDE`.
- `wlr_xdg_toplevel_decoration_v1_set_mode(deco, mode)` — call it folded into the
  toplevel `initial_commit` branch (and re-run from `request_mode`) so one atomic
  configure carries size + mode. CSD holdout = `requested_mode == CLIENT_SIDE`
  ⇒ set `CLIENT_SIDE`, `draw_frame = false`. A never-decorated client defaults to
  CSD (`has_decoration = false`). Also create `wlr_server_decoration_manager`
  (KDE protocol) defaulting to SERVER for older toolkits.

**fcft 3.3.3** (`/usr/include/fcft/fcft.h`):
- `bool fcft_init(FCFT_LOG_COLORIZE_NEVER, false, FCFT_LOG_CLASS_ERROR)` once;
  `fcft_fini()` at shutdown. No `fcft_log_init` in 3.3.3.
- `fcft_from_name(count, const char *names[static count], attributes) → fcft_font*`;
  use `attributes = "pixelsize=N"` (deterministic; `size=` is DPI-dependent).
  `fcft_destroy(font)`. Returns NULL on failure.
- `struct fcft_font { int height; int descent; int ascent; struct{int x,y;} max_advance; … }`
  — **`descent` is declared before `ascent`** (no positional init). No `space_advance`.
- `fcft_rasterize_char_utf32(font, cp, FCFT_SUBPIXEL_NONE) → const fcft_glyph*`
  (per-char path; sufficient for titles). Glyphs are font-owned/cached — do not free.
  `struct fcft_glyph { pixman_image_t *pix; int x,y,width,height; struct{int x,y;} advance; bool is_color_glyph; }`.
  `FCFT_SUBPIXEL_NONE` ⇒ `pix` is a `PIXMAN_a8` coverage mask (deterministic).
- **C++ hazard (verified):** `fcft.h` uses `[static count]` (identifier bound) and
  `restrict` — g++ rejects both even inside `extern "C"`. Fixed by the generalized
  sanitize target writing a shadowing `fcft/fcft.h` (mirrors `wlr_scene_compat`).
- **Link pixman explicitly** alongside `fcft` (we call `pixman_image_get_*` directly;
  `-lfcft` alone fails with a pixman DSO-missing link error).

**Compositing glyphs** (Option A, manual a8 blend over the opaque titlebar buffer):
baseline `y = ascent`; glyph top-left `dx = pen_x + glyph->x`, `dy = baseline -
glyph->y`; read coverage `a = mask[gy*pixman_image_get_stride(pix) + gx]`; blend
`out.{r,g,b} = (text*a + bg*(255-a) + 127)/255`, `out.a = 0xFF`; clip to label rect;
`pen_x += glyph->advance.x`. The `+127` round-to-nearest must match in tests for
byte-exact goldens.

---

## Determinism strategy (text)

fcft loads fonts **only by fontconfig family name** (no file-path loader). Pin via
fontconfig isolation (verified to force *every* family to the bundled file):

1. Commit `tests/fixtures/fonts/LiberationMono-Regular.ttf` (319 KB, SIL OFL 1.1) +
   its LICENSE.
2. Commit `tests/fixtures/fonts.conf`: single `<dir>` = that folder, a writable
   `<cachedir>`, and `<match target="font">` forcing `antialias=true hinting=true
   hintstyle=hintslight rgba=none autohint=false embeddedbitmap=false`.
3. Tests `setenv("FONTCONFIG_FILE", <abs path>)` (and clear `FONTCONFIG_PATH`)
   **before** `fcft_init`. Any family name resolves to the bundled font; render at
   `pixelsize=N`, `FCFT_SUBPIXEL_NONE`.

Authoritative guard = the **text unit test** (pinned glyph metrics + a8-mask
checksum). The frame golden runs under the same isolation, so it stays strict
(`tolerance=2`), with a small titlebar-scoped `pixel_budget` as defense-in-depth
against a future freetype point-release. The chosen `pixelsize` must yield
`fcft_font.height ≤ 19` (fits the label rect); pin `height/ascent/descent` in the
unit test so the geometry contract is machine-checked.

---

## Tasks (TDD; each compiles + tests + commits independently)

**T1 — Build plumbing: fcft dep + generalized sanitize + `toolkit/text.hpp`.**
Failing test: a compile-only TU including `toolkit/text.hpp` that calls
`fcft_init/fcft_fini` — fails to compile until the sanitized `fcft/fcft.h` target
exists. Production: generalize `tools/sanitize-wlr-scene.sh` to
`sed -E -e 's/\[static [a-zA-Z0-9_]+\]/[]/g' -e 's/\brestrict\b//g'`; add
`fcft_dep = dependency('fcft', '>=3.0.0')`; add `custom_target('fcft.h', …)` writing
a sanitized `fcft/fcft.h`; create `toolkit/text.hpp` (`extern "C"` over the
sanitized header + `<pixman.h>`); link `pixman_dep` explicitly with `fcft_dep`.
Verify the existing wlr build still compiles.
Commit: `build: fcft dep + generalized [static]/restrict sanitize, toolkit/text.hpp boundary (M3)`

**T2 — Seat/cursor/decoration headers in `toolkit/wlr.hpp`.**
Failing test: compile-only TU referencing `wlr_xdg_decoration_manager_v1`, `wlr_seat`,
`wlr_cursor`, `wlr_xcursor_manager`, `WLR_EDGE_BOTTOM`. Production: add `wlr_seat.h`,
`wlr_cursor.h`, `wlr_xcursor_manager.h`, `wlr/util/edges.h`, `wlr_xdg_decoration_v1.h`,
`wlr_server_decoration.h` inside the `extern "C"` block; add `xkbcommon` dep to meson.
Commit: `toolkit/wlr.hpp: add seat, cursor, edges, xdg-decoration, server-decoration headers (M3)`

**T3 — `bt::TextRenderer` + bundled font + determinism harness.**
Failing test `tests/unit/text_test.cc`: set isolated `FONTCONFIG_FILE` before
constructing `bt::TextRenderer("monospace", PIXELSIZE)`; assert `ascent/descent/height`
equal pinned constants (the ones that fit 19px), assert an FNV checksum over the a8
coverage of `"Ag1"` and `textWidth(U"Ag1")` equal blessed constants. Production:
`toolkit/Text.{hh,cc}` — refcounted `fcft_init` (`std::once`/counter), `fcft_from_name`
with `pixelsize`, `ascent()/descent()/height()/textWidth(u32string_view)`,
`drawText(vector<uint32_t>&, bufW, bufH, penX, baselineY, u32string_view, bt::Color)`
doing the Option-A a8 blend. Commit the TTF + LICENSE + `fonts.conf`.
Commit: `toolkit: bt::TextRenderer over fcft + bundled LiberationMono + isolated-fontconfig determinism test (M3)`

**T4 — Blackbox SSD frame renderer into the View scene tree + frame golden (no interaction).**
Failing test `tests/system/frame_test.cc` (clone `client_test.cc` + isolated
`FONTCONFIG_FILE`): map a 200×150 client at (160,120), pump to mapped, capture,
assert frame size 202×179 + named element pixels (titlebar gradient, a label-glyph
pixel, handle bar, grip), then `compareGolden(f, "tests/golden/m3-frame-ssd.png", 2,
small_budget)`. Production: rewrite `View.cc` to own `frame_tree`
(`node.data=this`); `wlr_scene_xdg_surface_create(frame_tree, base)` at `(bw,TH)`;
one `wlr_scene_buffer` per element via the seam with the textures above; label via
`drawText`; a `Decoration` helper that (re)builds+positions all buffers for a given
`frame_w/frame_h`; `View::setPosition(x,y)`. Draw 3 buttons, wire none yet.
Commit: `src/View: own scene tree, draw Blackbox SSD frame (titlebar+text+handle+grips+border) + frame golden (M3)`

**T5 — xdg-decoration global + SSD/CSD policy + golden.**
Failing test `tests/system/decoration_test.cc`: (i) a SERVER_SIDE/no-preference
client → after ack `current.mode==SERVER_SIDE` and frame drawn; (ii) a CLIENT_SIDE
holdout → `current.mode==CLIENT_SIDE`, **no** decoration buffers, xdg surface at the
View origin, geometry still managed. (Extend `TestClient` to bind
`zxdg_decoration_manager_v1` + `set_mode`.) Production: `Server` creates
`wlr_xdg_decoration_manager_v1_create` + `wlr_server_decoration_manager` (default
SERVER); `new_toplevel_decoration` routes the deco to its View by `deco->toplevel`;
`View::attachDecoration` + `chooseDecorationMode` folded into the initial_commit
handler; `has_decoration`/`draw_frame` flags; `request_mode`/`destroy` listeners with
`bt::Listener` self-disconnect; skip all decoration buffers when `!draw_frame`.
Commit: `src/Server+View: xdg-decoration manager, request-SSD/honor-CSD policy + golden (M3)`

**T6 — Seat/cursor/input plumbing + test injection API (no grab yet).**
Failing test `tests/system/hittest_test.cc`: map an SSD client; inject pointer motion
over the titlebar → `viewAtForTest` returns the View + classifies Titlebar; over a
grip → Grip(edges); over the client area → Client surface (with the pointer cap, the
client got `wl_pointer.enter`). Production: `Server` owns `wlr_seat`
(`POINTER|KEYBOARD` caps unconditionally), `wlr_cursor` (attached to output_layout),
`wlr_xcursor_manager`; `backend->events.new_input` wired (device dispatch; no-op
headless); cursor-event lambdas → `onPointerMotion/onPointerButton`; `nowMsec()`
monotonic seam; `injectPointerMotionForTest`/`injectPointerButtonForTest`;
`viewFromNode` parent-walk; per-grip `wlr_edges`; `CursorMode` + grab state members.
Commit: `src/Server: seat+cursor+input, scene hit-test to View/part, test injection API (M3)`

**T7 — Interactive MOVE (titlebar drag) + golden.**
Failing test `tests/system/move_test.cc`: map SSD client at (160,120); inject
titlebar press, motion +40/+30, release; assert frame tree + decorations now at
(200,150), terminating release NOT forwarded to the client; `compareGolden(f,
"tests/golden/m3-move.png", 2, …)`. Production: `Server::beginInteractive(v, Move, 0)`
from a titlebar press (and `tl->events.request_move` after grab-serial validation);
`processMove()` = `setPosition(grab_geo + (cursor - grab_origin))`; release ends the
grab and swallows the event.
Commit: `src/Server+View: interactive move via titlebar drag + golden (M3)`

**T8 — Interactive RESIZE (grip drag) + golden.**
Failing test `tests/system/resize_test.cc`: map SSD client; inject right-grip press,
motion +40/+20, release; `client.pump()` to let the client ack + recommit; assert
`tl->current.width/height` grew, frame re-laid-out, `compareGolden(f,
"tests/golden/m3-resize.png", 2, …)`. Second case: left grip moves top-left x while
anchoring the right edge. Production: `beginInteractive(v, Resize, edges)` from a grip
press (`BOTTOM|LEFT` / `BOTTOM|RIGHT`) and `tl->events.request_resize`;
`processResize()` clamped `w,h≥1`, `wlr_xdg_toplevel_set_resizing(tl,true)` on
start/`false` on release; **the View commit handler re-layouts + re-renders all
decoration buffers** at the new `frame_w/frame_h` and (for LEFT) repositions the
frame against `current.width`.
Commit: `src/Server+View: interactive resize via grip drag, decoration re-layout on commit + golden (M3)`

**T9 — Coverage top-up.** gcov; add unit tests for uncovered branches
(`TextRenderer` empty/clip/color-glyph flag, `chooseDecorationMode` no-deco path,
focus/unfocus texture swap, resize clamp at min size). Keep combined ≥80%.
Commit: `tests: cover decoration policy, text edge cases, resize clamp — keep >=80% line coverage (M3)`

**T10 — CI deps.** Add `fcft-devel` (+ `xkbcommon` if absent) to
`.github/workflows/ci.yml`. The committed font + isolated fontconfig make the host
font irrelevant; verify the suite is green in CI.
Commit: `ci: install fcft-devel + xkbcommon for M3 text/input (M3)`

**T11 — Milestone commit.** Full suite green incl. M1 background golden and the
**re-blessed** M2 golden (the M2 client is now decorated — re-bless it and update its
pixel assertions, client area now at (161,143); documented in the commit body as
proof the M2 client gains the M3 frame).
Commit: `milestone: M3 SSD frame + interactive move/resize + decoration policy complete`

---

## Acceptance criteria

`meson test` (headless, `WLR_BACKENDS=headless`, `WLR_RENDERER=pixman`) green, with:

1. **Pixel-accurate SSD frame** over the M1 gradient matching `m3-frame-ssd.png`
   (strict `tolerance=2`), frame size + element offsets per the geometry above
   (200×150 ⇒ 202×179; titlebar 23, handle 6, grips 38), with real fcft title text.
2. **Interactive move** — titlebar press+drag+release moves window+frame by the
   delta; terminating release not forwarded; `m3-move.png`.
3. **Interactive resize** — grip press+drag+release resizes the client (set_size →
   ack → recommit) with decorations re-laid-out at the new size; bottom-left anchors
   the right edge, bottom-right anchors the left; `m3-resize.png`.
4. **Decoration policy** — default `current.mode==SERVER_SIDE` + frame drawn; a
   CLIENT_SIDE holdout honored (`CLIENT_SIDE`, no chrome, geometry owned);
   `decoration_test.cc`.
5. **Text determinism** — text unit test passes (isolated fontconfig, bundled TTF,
   `pixelsize`, `FCFT_SUBPIXEL_NONE`) with pinned metrics + a8 checksum; chosen
   pixelsize `height ≤ 19`.
6. **No regressions** — M1 + M2 goldens pass (M2 re-blessed as decorated).
7. **Coverage** — combined `toolkit/`+`src/` line coverage ≥ 80%.
8. **Boundaries** — new wlroots/wayland headers only in `toolkit/wlr.hpp`; fcft/pixman
   only in `toolkit/text.hpp`; the generalized script sanitizes both `wlr_scene.h`
   and `fcft.h`.

---

## Key risks → de-risking test

| Risk | De-risked by |
|---|---|
| fcft won't compile under C++ (`[static count]`/`restrict`) — verified real | T1 compile-only TU fails until the generalized sanitize target exists |
| Text raster non-determinism across hosts | T3 unit test pins metrics + a8 checksum under isolated fontconfig; frame golden inherits the isolation |
| Frame geometry off-by-one | T4 asserts exact frame size + named pixels + golden (worked example is the oracle) |
| Decoration policy wrong (force chrome on CSD / fail SSD) | T5 drives both a SERVER_SIDE and a CLIENT_SIDE client |
| Decoration object arrives out of order vs the View | T5 uses a real `set_mode` roundtrip; `chooseDecorationMode` re-runs from `request_mode` + initial_commit |
| Injection tests a parallel path, not the real grab machinery | T6/7/8 inject through the same `onPointer*` bodies the real cursor events call |
| Resize anchor / stale decorations (left grip jumps; one-frame mismatch) | T8 captures after `client.pump()`; second case asserts left-grip anchors the right edge via the commit-handler reposition |
| M2 golden silently breaks when its client gains a frame | T11 makes the re-bless explicit and re-asserts M1+M2 |
| Teardown/listener leaks on decoration/seat destroy | M1/M2 teardown discipline + a close-window assertion on the decorated View; `bt::Listener` self-disconnect |
