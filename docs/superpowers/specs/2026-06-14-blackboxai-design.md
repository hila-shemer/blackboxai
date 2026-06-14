# BlackboxAI — Design Specification

**Status:** approved (architecture + roadmap), Milestone 1 detailed
**Date:** 2026-06-14
**Author:** Claude (Opus 4.8) with Hila Shemer

---

## 1. What we are building

**BlackboxAI** is a standalone **Wayland compositor** that reproduces the identity of the
classic *Blackbox* X11 window manager — its minimal, fast feel; the texture/gradient theming;
the toolbar (workspaces + window name + clock + iconbar); the root/window/workspace menus;
workspaces; and the slit/dock — on a modern, future-proof foundation.

It is **not** an X11 window manager and **not** a port in the literal sense. On Wayland the
compositor *is* the display server, input router, and window manager fused into one process,
so the *mechanism* layer (X event loop, reparenting, server-side grabs) is rewritten. What
survives — and what makes it *Blackbox* — is the **policy, theming, and toolkit**: the
`bt::Image` gradient renderer, the `Texture` style spec, the `.blackboxrc`/style file format,
the menu/workspace/stacking data models, and the overall UX.

BlackboxAI is a *rewrite in the style of the original* `bbidulock/blackboxwm` C++ codebase
(vendored at `reference/blackboxwm`, commit `22c0762`, ~26k LOC), reusing that codebase's
toolkit verbatim where the code is X-independent.

### Locked product decisions

- **Language/library:** C++20 shell on the **wlroots 0.19** C library.
- **Config/theme compatibility:** **drop-in** — existing classic Blackbox `~/.blackboxrc` and
  style files load **unchanged** (same resource namespace, same style grammar). The binary is
  `blackboxai`; classic configs Just Work.
- **The slit is a first-class v1 goal:** a real dock implemented as a **StatusNotifierItem
  (SNI) D-Bus system tray** — the modern successor to the slit's XEMBED dockapps (which are
  X11-only and a documented non-goal). It gets its own milestone with its own tests.
- **Compositing** is native (wlroots composites; no external picom, no Xephyr).

---

## 2. The central insight: the renderer seam (verified against source)

This is the highest-leverage decision in the project and it has been confirmed by reading the
actual source.

`bt::Image`'s gradient functions (`dgradient`/`egradient`/`hgradient`/`pgradient`/`rgradient`/
`partial_vgradient`/`cdgradient`/`pcgradient`/`svgradient`) and `raisedBevel`/`sunkenBevel` are
**pure-CPU array math** filling a packed `RGB data[width*height]` buffer
(`lib/Image.cc:589`; `RGB` is an 8:8:8:8 struct at `lib/Image.hh:45` — already ARGB8888-shaped).
The **only** X coupling is the tail: `render()` ends in `renderPixmap()` (`lib/Image.cc:909`:
`XColorTable` + dither + `XCreateImage`/`XCreatePixmap`/`XPutImage`) plus a live border
`XDrawRectangle`.

On a truecolor Wayland compositor **that entire tail is dead weight** — palette mapping and
Floyd–Steinberg/ordered dithering exist only to squeeze truecolor onto ancient 8/16-bit X
visuals. We **delete** `XColorTable` and the dither paths and replace `renderPixmap()` with a
`renderBuffer()` that copies `RGB[]` into an ARGB8888 `wlr_buffer` fed to a `wlr_scene_buffer`.

**Consequences:**

- The identity-defining visual code — gradients, bevels, the `Texture` spec, the
  `.blackboxrc`/style format — **ports verbatim and renders pixel-identically** to X11 Blackbox.
- Two traps a naive port would hit, now designed around:
  1. **Solid/flat fills, bevel lines, the border rectangle, and interlace lines are drawn
     *live*** in the original (`XFillRectangle`/`XDrawLine`/`XDrawRectangle`) and never go
     through `Image`. So "just replace `render()`" would silently lose all non-gradient
     rendering. **We unify:** every decoration/toolbar/menu surface is composed —
     gradient + solid + bevel + border + interlace — into **one** `bt::Image` `RGB[]` buffer,
     uploaded once as a single `wlr_scene_buffer`.
  2. **Text is a separate fourth path** (Xft straight onto the X drawable). We rasterize glyph
     coverage and **alpha-composite it into the same `RGB[]` buffer** at the aligned origin, so
     a label and its textured background live in one buffer.
- ~2000 lines of gradient/bevel math become **directly unit-testable on pixels** (no compositor
  needed) — the foundation of our coverage target.

### Verified libbt port map

| `lib/` file | LOC | X-call lines | Disposition |
|---|---:|---:|---|
| `Rect`, `Unicode` | 124+237 | 0 | **Port verbatim** |
| `Util`, `Resource`, `XDG` | 218+220 | 1–3 | Port ~verbatim (filesystem/parsing only) |
| `Texture` | 299 | 14 | Spec + `setDescription()` parser **verbatim** (X-free); only the `drawTexture()` blit helper is reimplemented |
| `Color` | 316 | 5 | Values port; drop X pixel allocation (truecolor only) |
| `Image` | 1970 | 33 | Gradient/bevel core **verbatim**; X tail (`XColorTable`, SHM, dither) **deleted**, replaced by `renderBuffer()` |
| `Pen`, `Bitmap` | 173+214 | 14+8 | Small reimplement → direct `RGB[]` writes |
| `Menu` | 1311 | 15 | Geometry/model **kept**; drawing → scene tree, popup grab → internal modal input |
| `PixmapCache` | 286 | 2 | Re-keyed identically; caches `wlr` textures |
| `Font` | 597 | 45 | **Full rewrite:** Xft → fcft+harfbuzz compositing glyphs into the buffer |
| `Display`, `EventHandler`, `EWMH` | — | — | Rewrite: X connection → wl_display/wlroots; EWMH → ext-workspace-v1 later |

---

## 3. Architecture

### 3.1 The compositor owns all the chrome

Because BlackboxAI *is* the compositor, everything that was an X child window — the desktop
background, window frames (titlebar/label/buttons/handle/grips), the toolbar, pop-up menus, and
the slit — is **compositor-owned `wlr_scene` content**, never a client surface. No client ever
draws our chrome.

Fixed scene layer order (each a `wlr_scene_tree` child of the root scene):

```
overlay     (menus, interactive drag feedback)
top         (toolbar, slit)
windows     (client xdg_surface trees + their decoration frames)
bottom      (docked layer-shell clients, if any)
background  (desktop texture)
```

### 3.2 wlroots 0.19 bring-up (modern API shapes)

- Split creation: `wlr_backend_autocreate(event_loop, NULL)` →
  `wlr_renderer_autocreate(backend)` → `wlr_allocator_autocreate(backend, renderer)`.
- `wlr_compositor_create`, `wlr_subcompositor_create`, `wlr_data_device_manager_create`.
- `wlr_scene_create`, `wlr_output_layout_create(display)`,
  `wlr_scene_attach_output_layout`.
- Output: `wlr_output_init_render`; build a `wlr_output_state`
  (`set_enabled`/`set_mode`/`set_scale`) → `wlr_output_commit_state`; `wlr_scene_output_create`.
- Frame loop: `wlr_scene_output_commit` + `wlr_scene_output_send_frame_done`.
- xdg-shell: `wlr_xdg_shell` + `new_toplevel`/`new_popup`; wrap client surfaces with
  `wlr_scene_xdg_surface_create`.
- Input: `wlr_seat`, `wlr_cursor`, `wlr_xcursor_manager`, `wlr_keyboard` + xkbcommon; hit-test
  via `wlr_scene_node_at`; keybindings dispatched in the seat handler (replaces `XGrabKey`).

### 3.3 C++/wlroots interop rules (project-wide)

- `-DWLR_USE_UNSTABLE` on every TU (set once in `meson.build`).
- **One** boundary header `toolkit/wlr.hpp` includes all wlroots/wayland/xkb C headers wrapped
  in `extern "C"`; no other file includes them directly. It also houses the wlroots-version shim.
- No C designated-initializer compound literals in C++ TUs; rename C-keyword-collision fields
  (`class`, `namespace`, …) at the boundary; `-fpermissive` is **banned** (documented last
  resort only).
- The `wl_listener`-in-struct idiom is wrapped in a single RAII `Listener<Owner>` template
  (ported from Wayfire's `wl_listener_wrapper`: embeds the `wl_listener` + a self back-pointer,
  static thunk does `wl_container_of`, `disconnect()` in the destructor, non-copyable/-movable).
  This maps the C signal plumbing onto the existing `bt::EventHandler` callback style.

### 3.4 Module / directory layout (mirrors `libbt` / `src`)

```
meson.build              project(cpp_std=c++20, warning_level=3, werror=false);
                         -DWLR_USE_UNSTABLE; subdir(protocols toolkit src data doc tests)
meson_options.txt        tests(bool,true); xwayland(bool,false); slit-sni(bool,true)
protocols/               wayland-scanner codegen: xdg-shell, xdg-decoration (from
                         wayland-protocols), vendored wlr-layer-shell; test-only:
                         single-pixel-buffer, virtual-keyboard/pointer, ext-image-copy-capture
toolkit/  (static_library 'bt')
  wlr.hpp                the single extern "C" + WLR_USE_UNSTABLE boundary header + version shim
  listener.hpp           RAII Listener<Owner> + wl_idle/wl_timer wrappers
  Image.{hh,cc}          gradient/bevel CPU math VERBATIM; renderBuffer() replaces renderPixmap()
  Texture.{hh,cc}        spec + setDescription() VERBATIM; drawTexture() → single-pass RGB[] compose
  Color/Resource/Rect/Util/Unicode/Bitmap/XDG   port ~verbatim
  Font.{hh,cc}           public API kept; Xft backend → fcft+harfbuzz into the buffer
  PixmapCache.{hh,cc}    same cache key; caches wlr textures
  Menu.{hh,cc}           geometry/model kept; drawing → scene tree; popup → modal input mode
  Timer/EventHandler/Application   Timer backed by an INJECTABLE virtual clock for deterministic tests
src/  (executable 'blackboxai')
  main.cc                CLI parse → Server → run()
  Server.{hh,cc}         owns display/backend/renderer/allocator/scene/output-layout, protocol
                         globals, seat; creates the 5 layer trees; new_output/new_input/
                         new_toplevel/new_popup listeners; run()/terminate()
  Output.{hh,cc}         one wlr_output + wlr_scene_output; frame loop; paints background texture
  View.{hh,cc}           wlr_xdg_toplevel + Blackbox frame scene_tree; map/commit/destroy;
                         interactive move/resize; xdg-decoration SSD negotiation
  Popup.{hh,cc}          wlr_xdg_popup subtree
  Seat.{hh,cc}           cursor, focus, keybinding table (→ XGrabKey), menu modal mode (→ XGrabPointer)
  Keyboard.{hh,cc}       wlr_keyboard + xkb state
  Toolbar.{hh,cc}        scene_tree of bt::Image buffers; Timer drives clock node re-render
  Slit.{hh,cc}           geometry/placement/auto-hide + SNI tray host (see §5, M6)
  Workspace/StackingList.{hh,cc}   pure data models VERBATIM (5-sentinel-layer invariant)
  Rootmenu/Workspacemenu/Windowmenu/Clientmenu/Configmenu/Slitmenu/Toolbarmenu/Iconmenu
  BlackboxResource/ScreenResource.{hh,cc}   config loading (drop-in .blackboxrc/style/menu)
data/styles/             the original style files, installed verbatim (drop-in compat)
tests/unit/              doctest cases over toolkit + pure src logic (suite 'unit')
tests/system/            headless golden-PNG tests + libwayland test-client helper (suite 'system')
tests/harness/           reusable HeadlessFixture (capture + libpng + golden compare + BLESS=1)
doc/                     ported man page + kebab-case design docs
```

### 3.5 Build system

**Meson + Ninja** (the entire wlroots ecosystem is meson; `dependency()` resolves the versioned
`.pc`, the `wayland-scanner` `custom_target` idiom is copied from sway/labwc, `b_coverage` is
free). `werror=false` project-wide (warning_level=3 fights `WLR_USE_UNSTABLE` + generated
protocol code); optionally scope `-Werror` to `toolkit/`+`src/` only. Unit framework:
**doctest** (single-header, fast recompiles, clean gcov flush on normal exit). Coverage:
**gcovr** (Cobertura XML + HTML + native `--fail-under-line` gate).

---

## 4. Testing strategy — 4-layer pyramid, ~80% gcov

The renderer + data models + parsers are pure and easily covered, so **most coverage is earned
at Layer 0**.

- **L0 — pure unit tests, no compositor** (the bulk): gradient/bevel pixels asserted on exact
  `RGB[]` values; `Texture::setDescription()` parsing; `Resource` Xrm-syntax parsing;
  `StackingList` (invariant: `empty() == (size()==5)`); `Workspace` add/remove/focus; `Menu`
  geometry.
- **L1 — headless + pixman fixture** (reusable): `WLR_BACKENDS=headless` + `WLR_RENDERER=pixman`,
  one fixed 1280×720 scale-1 output, animations off, an **injectable virtual clock** (never
  wall-clock), event loop stepped manually via `wl_event_loop_dispatch` → bit-reproducible,
  GPU-free.
- **L2 — in-process pixel capture** (primary): `wlr_texture_read_pixels` (DRM_FORMAT_XRGB8888)
  → libpng → golden compare (tolerance 0–2 + max-differing-pixel budget; `BLESS=1` regenerates;
  actual+diff PNGs on failure). Mirrors Weston/sway reference-image testing. Exactly **one**
  `ext-image-copy-capture-v1` client test keeps the capture protocol honest.
- **L3 — synthetic clients + injected input**: minimal libwayland clients using
  `single-pixel-buffer` for exact known colors map an `xdg_toplevel`; headless input via
  `wlr_seat_*_notify` for fast keybinding/focus tests, plus one `virtual-keyboard-v1`/
  `virtual-pointer-v1` black-box test.

**Budget:** ~20% good-path + ~10% bad-path system tests (missing/garbled style, CSD client
refusing SSD, client that never commits, malformed config); L0 unit tests bring the combined
total to **~80%**.

**Gate:** `meson -Db_coverage=true` (let meson own the flags). Run `unit` then `system` *without*
deleting `.gcda` between them (gcc counters accumulate), then
`gcovr -r . build --filter toolkit/ --filter src/ --fail-under-line=80 --fail-under-branch=60`
(non-zero exit fails CI). The compositor main loop is driven manually in tests (no blocking
`wl_display_run`) so the process exits cleanly and gcov flushes.

**Golden text handling:** text regions use a pinned bundled test font with hinting off (decided
alongside the M3 text backend) or are excluded from comparison, to avoid freetype/fontconfig
variation.

**CI:** GitHub Actions, fedora:43 container, on push + PR. Install deps; `meson setup build
-Db_coverage=true -Dbuildtype=debug`; `ninja`; `meson test --suite unit`; then
`export XDG_RUNTIME_DIR=$(mktemp -d) && chmod 700 "$XDG_RUNTIME_DIR"` (wayland-server refuses to
start otherwise) and `meson test --suite system` with `WLR_BACKENDS=headless
WLR_RENDERER=pixman`; then the gcovr gate; upload actual/diff PNGs + coverage on failure.

---

## 5. Roadmap — milestones (each demoable & independently testable)

| # | Milestone | Effort | Demoable artifact |
|---|---|---|---|
| **M1** | **Headless textured background** (skeleton + renderer seam) | ~1.5–2 wk | `blackboxai --headless` renders a Blackbox gradient desktop; golden-PNG system test matches; unit tests assert exact gradient/bevel pixels |
| **M2** | Real client mapped & composited | ~1.5 wk | A terminal appears on the gradient background (no decorations yet); known-color golden test |
| **M3** | **Blackbox decorations** (the frame) | ~2.5 wk | Terminal in a pixel-accurate Blackbox frame; drag titlebar to move, grips to resize; text via fcft |
| **M4** | Toolbar + root menu + workspaces | ~3 wk | Ticking clock (injected in tests), right-click Rootmenu, workspace switching with correct restacking |
| **M5** | Style/config fidelity (drop-in) + slit geometry | ~2.5 wk | All original styles load unchanged; live re-theme via Configmenu; slit placement/auto-hide on screen edge |
| **M6** | **Slit dock — SNI/StatusNotifierItem D-Bus tray** (first-class) | ~2.5–3 wk | Real tray apps (Nextcloud/Telegram/…) appear in the slit; click/menu works; mock-SNI golden test |
| **M7** | Daily-drivable on DRM/KMS + wlroots 0.20 shim | ~3–4 wk | Log into BlackboxAI on a TTY; real apps, menus, toolbar, slit for a full session; CI still green headless |

**The slit (M6)** is promoted to a first-class goal per product decision. XEMBED dockapps are
X11-only and a **non-goal**; their spiritual successor is a **StatusNotifierHost** over D-Bus
(via `sd-bus`, present on Fedora) that watches `org.kde.StatusNotifierWatcher`, hosts
`StatusNotifierItem`s, renders their icons as scene content in the slit region, and proxies
activation / context menus. Tested with a mock SNI publisher on a private D-Bus session →
golden image of the rendered tray. (Optional later: allow `wlr-layer-shell` clients to dock.)

**Deferred sub-decisions (with current lean):** text backend → **fcft+harfbuzz** (confirm at
M3); workspace-export protocol → **ext-workspace-v1** (M7); Xwayland → **off** by default, build
option (M7); wlroots **0.20** bump → tracked sub-task in M7 (the version shim localizes it);
CSD-client policy → manage geometry-only, no forced chrome (confirm at M3).

---

## 6. Milestone 1 — detailed design (the first sub-project)

**Goal:** the smallest thing that builds, runs headless, renders recognizably-Blackbox pixels,
and is proven by a golden PNG — exercising the *entire* identity seam (bt:: RGB renderer →
`wlr_scene_buffer`) and the *entire* test harness, with **zero** window-management complexity.

**Scope (in):**
1. Top-level `meson.build`: `dependency('wlroots-0.19', version: ['>=0.19.0','<0.20.0'])`,
   `wayland-server`, `pixman-1`, `libpng`, `doctest`; `-DWLR_USE_UNSTABLE`;
   `subdir(toolkit src tests)`.
2. `toolkit/wlr.hpp` boundary header; `toolkit/listener.hpp` RAII `Listener<Owner>`.
3. Renderer seam: port `bt::Image` (gradient/bevel math verbatim) + `Image::renderBuffer()`
   (fills/returns ARGB8888; deletes dither/`XColorTable`); `bt::Texture` + `setDescription()`
   verbatim; `bt::Color`; `bt::Resource` with a standalone Xrm-syntax parser (no X connection,
   no libX11 link) so a style string like `BlackboxAI.desktop: raised gradient diagonal` +
   `color1`/`color2` parses.
4. `src/Server.cc`: `wl_display_create`; backend/renderer/allocator autocreate;
   `wlr_renderer_init_wl_display`; `wlr_scene_create`; `wlr_output_layout_create`;
   `wlr_scene_attach_output_layout`; create the 5 layer trees (only `background` used in M1).
5. `src/Output.cc`: on `new_output`, `wlr_output_init_render`; commit a 1280×720 scale-1 state;
   `wlr_scene_output_create`; render a `Texture` (from the loaded `Resource`) via
   `renderBuffer()` into an ARGB8888 `wlr_buffer` and attach a full-screen `wlr_scene_buffer` at
   the bottom of `background`; frame handler = `wlr_scene_output_commit` +
   `wlr_scene_output_send_frame_done`.
6. `tests/harness/HeadlessFixture`: explicit headless backend (or env), forced 1280×720 scale-1
   output, loop driven a fixed number of iterations (not free-running), in-process
   `wlr_texture_read_pixels` → CPU buffer → libpng → golden compare (tolerance + max-diff
   budget; `BLESS=1` regen); on failure write actual + diff PNGs.

**Scope (out):** no clients, no `xdg_shell`, no input, no decorations, no toolbar/menus/slit.

**Acceptance criteria (all agent-verifiable, no human in the loop):**
1. `meson setup build && ninja -C build` succeeds against wlroots-0.19.
2. `meson test --suite system` runs the headless fixture, captures one frame, and matches
   `tests/golden/m1-background-diagonal-gradient.png` (bit-exact under pixman; tolerance 0–2).
3. `meson test --suite unit` passes: exact RGB asserted at the four corners + midpoint of a
   diagonal gradient and at a raised-bevel edge (proving the reused fill math, independent of the
   compositor); plus a `Texture`/`Resource` parse test
   (`raised gradient diagonal interlaced border` → the right flag bits + colors).
4. Running nested (`WAYLAND_DISPLAY` set, no flag) shows the same gradient in a window — manual
   sanity only.

**Why this M1:** if the gradient is pixel-correct here, every later surface (frames, toolbar,
menus, slit) reuses the same proven path, and the golden-PNG harness is in place from day one.

---

## 7. Build host dependencies (Fedora 43)

```
sudo dnf install -y meson ninja-build gcc-c++ gcovr pkgconf-pkg-config \
  wlroots-devel wayland-devel wayland-protocols-devel pixman-devel \
  libinput-devel libxkbcommon-devel libpng-devel doctest-devel
# M3 text:        fcft-devel harfbuzz-devel        (recommended; or pango-devel cairo-devel)
# M6 slit/SNI:    systemd-devel                    (sd-bus)
# M7 hardware:    libdrm-devel mesa-libgbm-devel xorg-x11-server-Xwayland (optional)
```

Notes: `wlroots-devel` provides `pkgconfig(wlroots-0.19)` = 0.19.3, headers under
`/usr/include/wlroots-0.19/`. Coverage tool is `gcovr` (not lcov). No Xvfb, no GPU, no X11
`-devel` needed — headless + pixman replaces them. Headless tests need a valid
`XDG_RUNTIME_DIR` owned by the user, mode 0700.

---

## 8. Risks & mitigations

- **wlroots ABI churn** (renames `.pc`/soname every minor): pin `wlroots-0.19`, gate the version,
  architect against the 0.18+ shapes (identical in 0.19/0.20), isolate any version-specific glue
  behind the shim header. 0.20 bump is a scheduled M7 sub-task, not a rewrite.
- **C++/C interop friction:** centralized in `toolkit/wlr.hpp` + the `Listener` template; rules in §3.3.
- **Scope drift on the slit:** SNI is a real D-Bus subsystem; it is isolated to M6 with its own
  tests and does not block earlier milestones.
- **Golden-image flakiness from fonts:** pixman is bit-exact for non-text; text uses a pinned
  font (hinting off) or is excluded from comparison.
- **Agent correctness:** every milestone has a build-exit-code + golden-PNG acceptance gate and a
  unit-test floor; the 80% gcov gate is enforced in CI from M5 onward (and unit tests exist from M1).
```
