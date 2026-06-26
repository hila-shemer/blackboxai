# Region screenshot to clipboard (Super+F7)

_Design spec, 2026-06-23. Feature on `wayland-rewrite`, on top of the wlroots 0.20
port + the four deferred M4 follow-ups. Companion to
`docs/superpowers/specs/2026-06-14-blackboxai-design.md` (architecture) and the
gotchas memory._

## What and why

One feature: press **Super+F7**, drag a rectangle, and the pixels under it land on
the clipboard as a PNG. That is the whole user story - the GNOME interactive-region
screenshot, minus the parts the user never touches.

The motivation is concrete: the running compositor advertises no screencopy, so
nothing outside it (grim, OBS, a portal) can capture the desktop, and the compositor
itself can't either. Rather than wire up the full `wlr-screencopy`/portal stack for a
capability the user invokes by hand a few times a day, bake the one path they want
directly into the compositor: trigger -> select -> clipboard.

This is also the first time the compositor reads its own framebuffer back outside the
headless test harness, and the first time it *owns* a clipboard selection. Both are
small, self-contained mechanisms; this spec pins down exactly where they sit.

## Non-goals (deliberately)

- **Whole-window / whole-screen / fixed-size modes.** Region drag only. The user
  explicitly never uses the others.
- **Auto-saving a file.** Clipboard only. No `~/Pictures/Screenshots`, no filename
  policy, no "saved to..." notification.
- **Multi-monitor region spanning.** One output exists today (M1). The capture reads
  the active output; a selection is clamped to it. Genuine multi-output is a later
  concern and does not constrain this design.
- **Annotation / editing / delay timer / cursor-in-shot.** None. GNOME hides its own
  UI and the pointer before capturing; so do we.

## Interaction model

A new compositor mode, `CursorMode::ScreenshotSelect`, sits alongside the existing
`Move`/`Resize` grabs and the modal-menu gate. It is gated first in
`onPointerButton` / `onPointerMotion` / `onKey`, exactly like those - the mode owns
input while it is active and the client sees nothing.

- **Enter:** `Action::Screenshot` (bound `Mod4+F7`) arms the mode. Pointer cursor
  switches to `crosshair` via the xcursor manager. No view is grabbed; this is a
  desktop-level interaction, not a per-window one.
- **Press** (left button) anchors corner A at the cursor.
- **Drag** updates corner B and redraws the selection overlay each motion event.
- **Release** anchors corner B, tears down the overlay, captures, encodes, and sets
  the clipboard. Mode exits to `Passthrough`.
- **Cancel** - Escape or right-click, at any point - tears down the overlay and exits
  with no capture. A press-release with effectively no drag (selection smaller than a
  few px on either axis) is treated as a cancel, not an empty capture.

Entering the mode while a Move/Resize grab is somehow live aborts that grab first
(same defensive move the modal menu already makes), so a stray terminating release
can't be eaten by the wrong gate.

## Selection overlay (GNOME-style dim)

While dragging, the screen dims except for the selected rectangle, which reads at full
brightness - the chosen feedback. `wlr_scene_rect` paints a single solid RGBA and
can't punch a hole, so the dim is **four rects** framing the clear region (above /
below / left / right of the selection), each a translucent black (alpha ~0.35),
parented under a dedicated tree on `layer_overlay`. On each drag motion the four rects
are repositioned to the new selection; on release/cancel the tree is destroyed.

The overlay tree is destroyed **before** the capture render, so the dim never appears
in the shot. The pointer is not a scene node in this compositor (it's a
`wlr_cursor` + xcursor), so it is naturally absent from the readback - matching
GNOME, which also excludes the cursor.

Geometry lives in a small pure helper (`screenshot::dimRects(output, selection) ->
4 boxes`) so the four-rects arithmetic is unit-testable without a scene.

## Capture (renderer-agnostic)

New `src/Screenshot.{hh,cc}`. The headless harness reads frames with
`wlr_buffer_begin_data_ptr_access`, which only works on mappable shm/pixman buffers -
fine for tests pinned to `WLR_RENDERER=pixman`, useless on the GL renderer the real
nested/DRM compositor runs. So capture takes the renderer-agnostic path:

1. `wlr_scene_output_build_state(scene_output, &state, opts)` renders the current
   scene into `state.buffer` (a GBM/DMABUF under GL, shm under pixman).
2. `wlr_texture_from_buffer(renderer, state.buffer)` imports it read-only.
3. `wlr_texture_read_pixels(tex, &opts)` with `src_box` = the clamped selection and a
   CPU-readable format (ARGB8888) copies just that sub-rectangle into a CPU buffer.
   `read_pixels` is implemented for both GL (glReadPixels-backed) and pixman, so one
   code path serves the real run and the tests.

`src_box` is clamped to the output bounds before the read. Output transform/scale is
assumed identity (true today); a non-trivial transform is out of scope and noted as a
load-bearing assumption rather than handled.

The result is an ARGB8888 CPU buffer of the selected size, alpha forced opaque (the
desktop has no meaningful alpha and a transparent screenshot is a surprise, not a
feature).

## PNG encode

The capture buffer is encoded to an **in-memory** PNG. libpng is already a build
dependency for the test harness; this lifts the RGBA-write path the harness already
has into a small reusable encoder in `src/` (`screenshot::encodePng(pixels, w, h) ->
std::vector<uint8_t>`) and points the harness at the same code, so there is one PNG
writer, not two. Encoding to a memory buffer (not a file) uses libpng's write
callback rather than `png_init_io`.

## Clipboard (server-side data source)

The compositor must put bytes on the clipboard *itself*, with no client involved -
so it implements a server-side `wlr_data_source`. New `src/ClipboardImage.{hh,cc}`:

- A `wlr_data_source` subtype offering exactly one MIME type, `image/png`, owning the
  encoded PNG bytes (shared, so in-flight reads outlive a replacement source).
- `wlr_seat_set_selection(seat, &source.base, wl_display_next_serial(display))`
  installs it as the clipboard selection.
- On a paste, wlroots invokes the source's `send(source, "image/png", fd)`. The bytes
  can exceed the pipe buffer and a slow reader must not stall the compositor, so the
  write is **asynchronous**: set `fd` non-blocking, register
  `wl_event_loop_add_fd(loop, fd, WL_EVENT_WRITABLE, ...)`, drain in chunks across
  callbacks, and remove the source + close on completion or hangup. Each paste gets
  its own writer context; the PNG bytes are shared via refcount so a request in flight
  survives the next screenshot replacing the selection.
- `destroy` frees the source once wlroots is done with it (another client claims the
  selection, or the compositor sets a newer one).

The byte-serving is factored so a test can drive it directly: hand the source a pipe
fd, run the writer to completion, read the other end, and assert the bytes decode as a
PNG of the expected size. The async event-loop wiring is the only part that needs the
real `wl_event_loop` (present in headless too - the test display is real).

## Module layout

| Unit | Responsibility | Depends on |
|---|---|---|
| `src/Screenshot.{hh,cc}` | scene-region readback (`captureRegion`), `encodePng`, pure `dimRects` | wlroots render/scene, libpng |
| `src/ClipboardImage.{hh,cc}` | server-side `wlr_data_source` for `image/png`, async fd writer | wlroots data-device, wl event loop |
| `src/Server` (edits) | `CursorMode::ScreenshotSelect` state machine, overlay tree, `Action::Screenshot` dispatch | the two above |
| `src/Keybindings` (edit) | `Action::Screenshot` kind + `Mod4+F7` default | - |

`Screenshot` is pure-ish (pixels in, pixels/bytes out) and holds no Server state.
`ClipboardImage` knows the seat and the event loop but nothing about selection
geometry. The Server glues them: it owns the mode, the overlay, and the lifetime of
the current `ClipboardImage`.

## Testing (headless, keeps the ~90% gate)

- **Region crop:** map the red test client at a known spot, `captureRegion` a
  sub-rectangle, assert the cropped pixels (red inside the window, desktop gradient
  outside) - a golden or direct pixel assert.
- **PNG round-trip:** `encodePng` then decode, assert dimensions and a sample of
  pixels survive.
- **`dimRects`:** pure - feed an output size and a selection, assert the four boxes
  tile the complement exactly (no overlap, no gap, union = output minus selection).
- **Clipboard source:** construct a `ClipboardImage` over known PNG bytes, run its
  writer against a pipe, read + decode the other end, assert MIME `image/png` and byte
  equality.
- **Select state machine:** `injectKeyForTest(F7, Mod4)` to arm, `injectPointer*` to
  press/drag/release, assert the mode transitions, that the overlay tree appears and
  is gone after release, and that a sub-px drag cancels. The seat-level
  forward-to-client suppression while modal is covered the same way the menu gate is.

Headless-untestable, documented not chased: the real GL `read_pixels` path (tests run
pixman - same call, different backend) and the live paste from a foreign client
(needs a second real client round-trip; the in-process pipe test covers the serving
logic). Both are exercised by hand in a nested run before pushing.

## Open assumptions (load-bearing)

- Identity output transform/scale. A rotated or scaled output would need `src_box` in
  the post-transform space; out of scope.
- The selection is captured from a single active output. Correct for M1; revisit with
  real multi-output.
- `wlr_scene_output_build_state` re-renders enough to read back a correct frame
  outside a normal commit cycle. The harness already relies on this headless; this
  spec extends the reliance to GL, to be confirmed in the nested smoke run.

## Research-verified corrections (2026-06-26)

A design-research pass compiled and ran the capture path as standalone C (pixman) and
verified the clipboard contract against cloned wlroots 0.20 source. What it changed:

**Capture.** `wlr_scene_output_build_state(so, &st, nullptr)` - no options struct.
Reading into `DRM_FORMAT_ARGB8888` already returns alpha 0xff per pixel, so the
"force opaque" is a cheap guard, not a required step. `wlr_texture_from_buffer` takes
its *own* buffer lock, so there is no fragile destroy-vs-finish ordering invariant
(cleanest is still read_pixels -> destroy texture -> finish state, but it's style).
`wlr_texture_read_pixels_options.src_box` is a **const member** - build the whole
options aggregate in one initializer with the clamped box already computed; you cannot
assign `opts.src_box` after construction (it won't compile). `captureRegion` needs
`#include <drm_fourcc.h>` (libdrm, already pulled via `wlr_dep`) for the format
constant. `read_pixels` returns `bool` and *can* fail - notably a GL backend lacking
`GL_EXT_read_format_bgra` rejects the ARGB read rather than mis-ordering; check the
return. Pixman always succeeds; GL is the nested-run unknown. Note this is a genuinely
*new* readback path - the harness's `captureFrame` uses `begin_data_ptr_access`
(pixman-only); `captureRegion` is the first in-tree use of `read_pixels`.

**PNG.** Drop the "one writer" unification - the harness writer is `FILE*`/`png_init_io`
and can't be lifted into an in-memory encoder verbatim. `encodePng` is a fresh
callback-based encoder (`png_set_write_fn` over a `std::vector`), channel order R,G,B,A
unpacked from `0xAARRGGBB` to match the harness convention so goldens don't shift.
`src/meson.build` must gain `dependency('libpng')` - today it's test-harness-only.

**Clipboard - the ownership trap.** `wlr_data_source` destroy is **synchronous and
re-entrant**: setting the next selection tears the old `ClipboardImage` down *inside*
`wlr_seat_set_selection`, before it returns. So the Server owns the `ClipboardImage`
via a **raw pointer with wlroots' `impl->destroy` as the sole deleter** - no owning
`unique_ptr`, or the second screenshot double-frees. wlroots keeps no awareness of
in-flight paste writers, so the refcounted PNG blob is load-bearing: the async writer
holds its *own* ref to the bytes and never dereferences the source after `send()`
returns. `destroy` frees only the C++ subtype - **not** `mime_types`/the strdup'd
string (wlroots already freed those before calling it) - and since `impl->destroy` is
set, wlroots will not `free(source)` for you. Do cleanup in `impl->destroy`, never via
a listener on `events.destroy` (wlroots asserts that list empty after emit). The paste
fd arrives **blocking**; the `O_NONBLOCK` fcntl in `send()` is mandatory, not optional.

**Server wiring.** There is no production active-scene-output getter; the Server glue
passes the `wlr_scene_output*` + `wlr_renderer*` into `captureRegion` (which holds no
Server state). Escape-cancel is not reachable through `injectKeyForTest` as written -
it needs a `ScreenshotSelect` branch in the inject path plus a `screenshotActiveForTest()`
accessor (right-click cancel already reaches `onPointerButton`). The crosshair is
net-new (`wlr_cursor_set_xcursor(cursor, xcursor_mgr, "crosshair")`); a theme without a
crosshair glyph is a silent no-op, eyeballed in the nested run. `wlr_scene_rect` color
is **premultiplied** - black-at-0.35 is fine, but a future tint must be authored
premultiplied. The overlay tree is **destroyed** (not hidden) before the capture render.

## Post-implementation review (2026-06-26): fixed + accepted

An adversarial-review workflow (five dimensions, each finding independently verified)
plus a nested GL smoke run shook out the following. **Fixed:**

- ScreenshotSelect copied the menu gate for keys and pointer but missed three things
  the menu gate handles, all now closed: `onModifiers` gates the mode (+ re-sync on
  exit) so a released modifier isn't stuck-down in the focused client; `beginScreenshot`
  clears pointer focus so a client holding a button at arm-time isn't stranded mid-drag
  (its button-up is swallowed while modal); and the exit paths re-resolve pointer focus.
- `captureRegion` guards against `build_state` direct-scanning-out a single opaque
  full-output buffer (it would hand back the client's own buffer, wrong-sized + offset):
  it bails on a texture whose dimensions don't match the output rather than reading
  mismatched coordinates. Effectively unreachable while the always-present toolbar keeps
  the scene list >1, but cheap insurance.
- Hygiene: `~Server` frees the dim overlay if a drag was live; `encodePng`'s `row` is
  declared before the `setjmp` so a libpng longjmp doesn't leak it; `ciSend` doesn't leak
  the fd+Writer if `wl_event_loop_add_fd` fails.
- Test rigor: the e2e now decodes the clipboard PNG and asserts the region dimensions +
  an un-dimmed window pixel (was: PNG magic only); the modal pointer-suppression test
  gained a positive control; a second-screenshot test exercises the re-entrant
  selection-replacement (double-free) path; the clipboard test tears down via the real
  `wlr_data_source_destroy` ordering.

**Accepted (documented, not chased):**

- The **real `onKey` body** for the ScreenshotSelect branch (the Escape match over
  `nsyms`, the `swallowed_keycodes_` press+release swallow, key suppression to the focused
  client) is headless-untestable without a fake `wlr_keyboard` - the exact limitation the
  project already accepts for the modal menu (gotchas memory #24). Tests drive the
  matcher + the modal nav via `injectKeyForTest`; the real keyboard path is eyeballed in
  the nested run. The production branch is consistent with the menu's, which is exercised.
- The **GL `read_pixels` failure path** (`px.empty()` -> no clipboard) is defensive. The
  GL path itself is **validated**: a nested run on the real GPU (`is_pixman=0`) returned
  `read_pixels(ARGB8888) -> 1` with correctly-ordered pixels - `GL_EXT_read_format_bgra`
  is present, so the documented GL risk does not bite here.
- The **in-flight paste `Writer` leak at compositor shutdown** is shutdown-only hygiene
  (the OS reclaims the fd + memory on exit); not worth a Writer-tracking registry.
