# Super+F7 region screenshot to clipboard — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Press Super+F7, drag a rectangle, and the pixels under it land on the clipboard as a PNG.

**Architecture:** A new `CursorMode::ScreenshotSelect` compositor mode (sibling to Move/Resize and the modal-menu gate) owns input while active: press anchors, drag repaints a GNOME-style four-rect dim overlay, release captures. Capture is renderer-agnostic — `wlr_scene_output_build_state` → `wlr_texture_from_buffer` → `wlr_texture_read_pixels(src_box)` → in-memory libpng encode. The PNG is owned by a server-side `wlr_data_source` (`ClipboardImage`) installed via `wlr_seat_set_selection`, served to pasting clients asynchronously over the event loop.

**Tech Stack:** wlroots 0.20.1, C++20, meson/ninja, doctest, libpng, libdrm fourcc.

## Global Constraints

- wlroots **0.20** (`wlr_dep`); all C wlroots/wayland headers come through `toolkit/wlr.hpp` — except `<drm_fourcc.h>` which is included directly (pattern: `src/DataBuffer.cc:3`).
- C++20. Namespace `bbai` (sub-namespace `bbai::screenshot` for the new unit). Header guard `BLACKBOXAI_<NAME>_HH`.
- TDD, **one green commit per task**: write the failing test, watch it fail, implement minimally, watch it pass, commit. Each task compiles + passes its suite before commit.
- Tests are **headless + pixman** (`WLR_BACKENDS=headless WLR_RENDERER=pixman`, set by `test_env`/`text_env` in `tests/meson.build`). Use `text_env` when a captured frame contains a decorated/red client window or title text; `test_env` for pure-state assertions.
- Coverage gate **80%** line (`gcovr -r . build --filter 'toolkit/' --filter 'src/' --fail-under-line=80`); project runs ~90%. Keep it there.
- `wlr_texture_read_pixels_options.src_box` is a **const member** — build the whole options struct in ONE designated initializer with the clamped box already computed; you cannot assign `opts.src_box` afterward.
- `wlr_scene_rect` color is **premultiplied** — dim black `{0,0,0,0.35f}` is fine; any future tint must be authored premultiplied.
- `ClipboardImage` is owned by **wlroots**, not the Server: install via `wlr_seat_set_selection` and never hold an owning smart pointer to it — wlroots' `impl->destroy` is the sole deleter (it runs synchronously when the next selection replaces it; an owning `unique_ptr` would double-free).
- The async paste writer holds its **own** ref to the refcounted PNG bytes and never dereferences the `wlr_data_source` after `send()` returns.

**Build / test commands** (used throughout):
```sh
ninja -C build                              # build
meson test -C build <name> -v               # run one suite/test (sets env + workdir)
./build/tests/unit-tests -tc="<case glob>"  # run one unit case directly (fail-first)
BLESS=1 meson test -C build <name>           # (re)generate a golden, if any
```

---

### Task 1: Pure `dimRects` geometry helper

**Files:**
- Create: `src/Screenshot.geom.hh`
- Test: `tests/unit/screenshot_geom_test.cc`
- Modify: `tests/meson.build` (add to `unit_sources`)

**Interfaces:**
- Produces: `namespace bbai::screenshot` with `struct Rect { int x,y,w,h; }`, `struct DimRects { Rect above,below,left,right; }`, and inline pure fns:
  - `Rect fromCorners(int ax,int ay,int bx,int by)` — two drag corners → positive-size rect.
  - `Rect clampToOutput(Rect sel,int ow,int oh)` — clamp to `[0,ow]×[0,oh]`, never negative w/h.
  - `DimRects dimRects(int ow,int oh,Rect sel)` — four rects tiling the output complement of `sel`.

- [ ] **Step 1: Write the failing test** — `tests/unit/screenshot_geom_test.cc`

```cpp
#include <doctest/doctest.h>
#include "Screenshot.geom.hh"

using namespace bbai::screenshot;

static long area(const Rect &r) { return (long)r.w * r.h; }
static bool overlaps(const Rect &a, const Rect &b) {
  return a.x < b.x + b.w && b.x < a.x + a.w &&
         a.y < b.y + b.h && b.y < a.y + a.h;
}

TEST_CASE("fromCorners normalizes any drag direction") {
  CHECK(fromCorners(10, 20, 110, 220).x == 10);
  CHECK(fromCorners(10, 20, 110, 220).w == 100);
  CHECK(fromCorners(110, 220, 10, 20).x == 10);   // dragged up-left
  CHECK(fromCorners(110, 220, 10, 20).w == 100);
  CHECK(fromCorners(110, 220, 10, 20).h == 200);
}

TEST_CASE("clampToOutput keeps the selection inside the output") {
  Rect c = clampToOutput({ -10, -10, 50, 50 }, 1280, 720);
  CHECK(c.x == 0); CHECK(c.y == 0); CHECK(c.w == 40); CHECK(c.h == 40);
  Rect d = clampToOutput({ 1260, 700, 100, 100 }, 1280, 720);
  CHECK(d.w == 20); CHECK(d.h == 20);
  Rect off = clampToOutput({ 2000, 2000, 50, 50 }, 1280, 720);
  CHECK(off.w == 0); CHECK(off.h == 0);            // fully off-screen
}

TEST_CASE("dimRects tile the complement of an interior selection") {
  const int OW = 1280, OH = 720;
  Rect sel{ 200, 150, 400, 300 };
  DimRects d = dimRects(OW, OH, sel);
  // No dim rect overlaps the selection.
  for (const Rect *r : { &d.above, &d.below, &d.left, &d.right })
    CHECK_FALSE(overlaps(*r, sel));
  // The four dim rects do not overlap each other.
  CHECK_FALSE(overlaps(d.above, d.left));
  CHECK_FALSE(overlaps(d.above, d.right));
  CHECK_FALSE(overlaps(d.below, d.left));
  CHECK_FALSE(overlaps(d.below, d.right));
  CHECK_FALSE(overlaps(d.left,  d.right));
  // Areas sum to output minus selection (exact tiling).
  long sum = area(d.above) + area(d.below) + area(d.left) + area(d.right);
  CHECK(sum == (long)OW * OH - area(sel));
}

TEST_CASE("dimRects degenerate cases produce no negative rects") {
  DimRects full = dimRects(1280, 720, { 0, 0, 1280, 720 });   // whole output
  for (const Rect *r : { &full.above, &full.below, &full.left, &full.right }) {
    CHECK(r->w >= 0); CHECK(r->h >= 0); CHECK(area(*r) == 0);
  }
  DimRects edge = dimRects(1280, 720, { 0, 0, 400, 300 });    // top-left corner
  CHECK(area(edge.above) == 0);   // nothing above
  CHECK(area(edge.left)  == 0);   // nothing left
  long sum = area(edge.above) + area(edge.below) + area(edge.left) + area(edge.right);
  CHECK(sum == (long)1280 * 720 - 400 * 300);
}
```

- [ ] **Step 2: Wire the test into the unit suite** — add `'unit/screenshot_geom_test.cc',` to the `unit_sources = files( ... )` list in `tests/meson.build`.

- [ ] **Step 3: Run it to verify it fails**

Run: `ninja -C build`
Expected: FAIL — `fatal error: Screenshot.geom.hh: No such file or directory`.

- [ ] **Step 4: Write the helper** — `src/Screenshot.geom.hh`

```cpp
// Pure region-screenshot geometry: drag-corner normalization, output clamping,
// and the four GNOME-style dim rects that frame the clear selection. No wlroots.
#ifndef BLACKBOXAI_SCREENSHOT_GEOM_HH
#define BLACKBOXAI_SCREENSHOT_GEOM_HH

namespace bbai::screenshot {

  struct Rect { int x = 0, y = 0, w = 0, h = 0; };
  struct DimRects { Rect above, below, left, right; };

  // Two opposite drag corners -> a positive-size rect (any drag direction).
  inline Rect fromCorners(int ax, int ay, int bx, int by) {
    const int x = ax < bx ? ax : bx;
    const int y = ay < by ? ay : by;
    const int w = ax < bx ? bx - ax : ax - bx;
    const int h = ay < by ? by - ay : ay - by;
    return { x, y, w, h };
  }

  // Clamp to [0,ow] x [0,oh]; never returns a negative width/height.
  inline Rect clampToOutput(Rect s, int ow, int oh) {
    if (s.x < 0) { s.w += s.x; s.x = 0; }
    if (s.y < 0) { s.h += s.y; s.y = 0; }
    if (s.x > ow) s.x = ow;
    if (s.y > oh) s.y = oh;
    if (s.x + s.w > ow) s.w = ow - s.x;
    if (s.y + s.h > oh) s.h = oh - s.y;
    if (s.w < 0) s.w = 0;
    if (s.h < 0) s.h = 0;
    return s;
  }

  // Four rects tiling the output minus `sel`: full-width bands above/below,
  // selection-height strips left/right. Degenerate bands collapse to zero area.
  inline DimRects dimRects(int ow, int oh, Rect sel) {
    const int selBottom = sel.y + sel.h;
    const int selRight  = sel.x + sel.w;
    DimRects d;
    d.above = { 0, 0,         ow,             sel.y };
    d.below = { 0, selBottom, ow,             oh - selBottom };
    d.left  = { 0, sel.y,     sel.x,          sel.h };
    d.right = { selRight, sel.y, ow - selRight, sel.h };
    for (Rect *r : { &d.above, &d.below, &d.left, &d.right }) {
      if (r->w < 0) r->w = 0;
      if (r->h < 0) r->h = 0;
    }
    return d;
  }

} // namespace bbai::screenshot

#endif // BLACKBOXAI_SCREENSHOT_GEOM_HH
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `ninja -C build && meson test -C build unit -v`
Expected: PASS (all four `screenshot_geom` cases green).

- [ ] **Step 6: Commit**

```bash
git add src/Screenshot.geom.hh tests/unit/screenshot_geom_test.cc tests/meson.build
git commit -m "Screenshot: pure dimRects geometry helper (four-rect GNOME dim)"
```

---

### Task 2: In-memory PNG encoder

**Files:**
- Create: `src/Screenshot.hh`, `src/Screenshot.cc`
- Modify: `src/meson.build` (add `Screenshot.cc` to `bbai_lib` sources + `dependency('libpng')`)
- Test: `tests/system/screenshot_png_test.cc`
- Modify: `tests/meson.build` (new system test exe)

**Interfaces:**
- Consumes: `bbai::screenshot::Rect` (Task 1).
- Produces: `std::vector<uint8_t> bbai::screenshot::encodePng(const std::vector<uint32_t> &px, int w, int h)` — row-major `0xAARRGGBB` in, PNG bytes out (empty on failure). Channel order written R,G,B,A.

- [ ] **Step 1: Write the failing test** — `tests/system/screenshot_png_test.cc`

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "Screenshot.hh"
#include <png.h>
#include <cstdio>
#include <cstring>

using namespace bbai::screenshot;

// Decode an in-memory PNG with libpng to verify the round trip.
namespace {
  struct MemReader { const uint8_t *p; size_t len, off; };
  void readCb(png_structp png, png_bytep out, png_size_t n) {
    auto *r = static_cast<MemReader *>(png_get_io_ptr(png));
    size_t take = n;
    if (r->off + take > r->len) take = r->len - r->off;
    memcpy(out, r->p + r->off, take);
    r->off += take;
  }
}

TEST_CASE("encodePng round-trips dimensions and corner pixels") {
  const int W = 4, H = 3;
  std::vector<uint32_t> in(W * H, 0xFF000000u);   // opaque black
  in[0]            = 0xFFFF0000u;                  // TL red
  in[W - 1]        = 0xFF00FF00u;                  // TR green
  in[(H - 1) * W]  = 0xFF0000FFu;                  // BL blue

  std::vector<uint8_t> png = encodePng(in, W, H);
  REQUIRE(png.size() > 8);
  CHECK(png_sig_cmp(png.data(), 0, 8) == 0);       // valid PNG magic

  // Decode back.
  png_structp p = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info = png_create_info_struct(p);
  MemReader mr{ png.data(), png.size(), 0 };
  png_set_read_fn(p, &mr, readCb);
  png_read_info(p, info);
  CHECK(png_get_image_width(p, info) == (png_uint_32)W);
  CHECK(png_get_image_height(p, info) == (png_uint_32)H);
  if (png_get_bit_depth(p, info) == 16) png_set_strip_16(p);
  png_set_filler(p, 0xFF, PNG_FILLER_AFTER);
  png_read_update_info(p, info);
  std::vector<png_byte> row((size_t)W * 4);
  png_read_row(p, row.data(), nullptr);            // first row
  CHECK(row[0] == 0xFF); CHECK(row[1] == 0x00); CHECK(row[2] == 0x00);   // TL red R,G,B
  CHECK(row[(W - 1) * 4 + 1] == 0xFF);             // TR green G
  png_destroy_read_struct(&p, &info, nullptr);
}
```

- [ ] **Step 2: Add the system test exe to `tests/meson.build`** (after the `close_button` block):

```meson
# Region screenshot: in-memory PNG encoder round-trip.
screenshot_png_exe = executable('screenshot-png-test',
  files('system/screenshot_png_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('screenshot_png', screenshot_png_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)
```

- [ ] **Step 3: Run it to verify it fails**

Run: `ninja -C build`
Expected: FAIL — `Screenshot.hh: No such file or directory`.

- [ ] **Step 4: Create the header** — `src/Screenshot.hh`

```cpp
// Region screenshot: scene readback of a sub-rectangle (renderer-agnostic) and
// an in-memory PNG encoder. Pure of Server state; the Server glue passes the
// scene output + renderer in. See Screenshot.geom.hh for the geometry.
#ifndef BLACKBOXAI_SCREENSHOT_HH
#define BLACKBOXAI_SCREENSHOT_HH

#include "wlr.hpp"
#include "Screenshot.geom.hh"

#include <cstdint>
#include <vector>

namespace bbai::screenshot {

  // Encode a row-major ARGB8888 (0xAARRGGBB) buffer as PNG bytes. Empty on error.
  std::vector<uint8_t> encodePng(const std::vector<uint32_t> &px, int w, int h);

  // Render the active scene and read back `sel` (output/layout pixels) into a
  // tightly-packed ARGB8888 buffer with alpha forced opaque. `sel` is clamped to
  // the output first; outW/outH receive the clamped size. Empty (outW=outH=0) on
  // failure or a degenerate region. Works on GL and pixman.
  std::vector<uint32_t> captureRegion(wlr_scene_output *so, wlr_renderer *renderer,
                                      Rect sel, int &outW, int &outH);

} // namespace bbai::screenshot

#endif // BLACKBOXAI_SCREENSHOT_HH
```

- [ ] **Step 5: Implement `encodePng`** — `src/Screenshot.cc` (captureRegion is a stub here; filled in Task 3)

```cpp
#include "Screenshot.hh"

#include <png.h>
#include <drm_fourcc.h>

#include <csetjmp>

namespace bbai::screenshot {

  namespace {
    void appendToVector(png_structp png, png_bytep data, png_size_t len) {
      auto *out = static_cast<std::vector<uint8_t> *>(png_get_io_ptr(png));
      out->insert(out->end(), data, data + len);
    }
  } // namespace

  std::vector<uint8_t> encodePng(const std::vector<uint32_t> &px, int w, int h) {
    std::vector<uint8_t> out;
    if (w <= 0 || h <= 0 || px.size() < static_cast<size_t>(w) * h) return out;

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) return out;
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_write_struct(&png, nullptr); return out; }
    // libpng error path. Keep no non-trivial-dtor locals live across this setjmp
    // except `out`, which we clear on error (mirrors the harness writePNG).
    if (setjmp(png_jmpbuf(png))) {
      png_destroy_write_struct(&png, &info);
      out.clear();
      return out;
    }
    png_set_write_fn(png, &out, appendToVector, nullptr);
    png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    std::vector<png_byte> row(static_cast<size_t>(w) * 4);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const uint32_t p = px[static_cast<size_t>(y) * w + x];
        row[x * 4 + 0] = (p >> 16) & 0xFF;  // R
        row[x * 4 + 1] = (p >> 8) & 0xFF;   // G
        row[x * 4 + 2] = p & 0xFF;          // B
        row[x * 4 + 3] = (p >> 24) & 0xFF;  // A
      }
      png_write_row(png, row.data());
    }
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    return out;
  }

  std::vector<uint32_t> captureRegion(wlr_scene_output *, wlr_renderer *,
                                      Rect, int &outW, int &outH) {
    outW = outH = 0;   // Task 3 fills this in.
    return {};
  }

} // namespace bbai::screenshot
```

- [ ] **Step 6: Wire `Screenshot.cc` + libpng into `src/meson.build`**

In the `bbai_lib = static_library(...)` `files( ... )` list add `'Screenshot.cc',`. In BOTH the `bbai_lib` `dependencies :` array and the `bbai_dep` `dependencies :` array, add `dependency('libpng')`. For example:

```meson
bbai_lib = static_library('bbai',
  files(
    'DataBuffer.cc',
    'Screenshot.cc',
    'Server.cc',
    # ...existing entries...
  ),
  include_directories : [src_inc],
  dependencies : [bt_dep, bttext_dep, wlr_dep, dependency('libpng')])

bbai_dep = declare_dependency(link_with : bbai_lib,
  include_directories : [src_inc, bt_inc],
  dependencies : [bt_dep, bttext_dep, wlr_dep, dependency('libpng')])
```

- [ ] **Step 7: Run the test to verify it passes**

Run: `ninja -C build && meson test -C build screenshot_png -v`
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add src/Screenshot.hh src/Screenshot.cc src/meson.build \
        tests/system/screenshot_png_test.cc tests/meson.build
git commit -m "Screenshot: in-memory libpng encoder (encodePng) + libpng dep on bbai_lib"
```

---

### Task 3: `captureRegion` scene readback

**Files:**
- Modify: `src/Screenshot.cc` (replace the `captureRegion` stub)
- Test: `tests/system/screenshot_capture_test.cc`
- Modify: `tests/meson.build`

**Interfaces:**
- Consumes: `encodePng` is unused here; uses `clampToOutput` (Task 1) + the wlroots capture chain.
- Produces: the real `captureRegion(wlr_scene_output*, wlr_renderer*, Rect, int&, int&)`.

- [ ] **Step 1: Write the failing test** — `tests/system/screenshot_capture_test.cc`

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"
#include "Screenshot.hh"

#include <cstdlib>

using namespace bbai;

static void mapOne(Server &server, test::TestClient &c, int iters = 500) {
  auto mapped = [&] {
    const auto &v = server.viewsForTest();
    return !v.empty() && v[0]->isMapped();
  };
  for (int i = 0; i < iters && !mapped(); ++i) { c.flush(); server.dispatch(); c.pump(); }
  for (int i = 0; i < 30; ++i) { c.flush(); server.dispatch(); c.pump(); }
}

TEST_CASE("captureRegion crops the scene: red window inside, gradient outside") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  // A red 200x150 client; new toplevels open centered-ish (default ~160,120 incl. frame).
  test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  REQUIRE(c.ok());
  mapOne(server, c);
  View *v = server.viewsForTest()[0].get();
  const int cx = v->x() + (v->drawsFrame() ? 1 : 0);   // a point inside the client area
  const int cyp = v->y() + (v->drawsFrame() ? 23 : 0); // below the 23px titlebar

  int rw = 0, rh = 0;
  auto px = screenshot::captureRegion(server.activeSceneOutputForTest(),
                                      server.renderer,
                                      { cx + 10, cyp + 10, 40, 30 }, rw, rh);
  REQUIRE(rw == 40); REQUIRE(rh == 30);
  REQUIRE(px.size() == (size_t)rw * rh);
  // Sampled center pixel is the client red, fully opaque.
  uint32_t mid = px[(rh / 2) * rw + (rw / 2)];
  CHECK((mid >> 24) == 0xFF);                    // opaque
  CHECK((mid & 0x00FFFFFFu) == 0x00FF0000u);     // red

  // A region entirely on the desktop reads the gradient, not red.
  auto bg = screenshot::captureRegion(server.activeSceneOutputForTest(),
                                      server.renderer, { 5, 5, 20, 20 }, rw, rh);
  REQUIRE(bg.size() == (size_t)rw * rh);
  CHECK((bg[0] & 0x00FFFFFFu) != 0x00FF0000u);
}

TEST_CASE("captureRegion clamps an over-edge selection and rejects degenerate") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();
  int ow = 0, oh = 0; server.activeOutputSize(ow, oh);

  int rw = 0, rh = 0;
  auto px = screenshot::captureRegion(server.activeSceneOutputForTest(), server.renderer,
                                      { ow - 10, oh - 10, 100, 100 }, rw, rh);
  CHECK(rw == 10); CHECK(rh == 10);
  CHECK(px.size() == 100);

  auto empty = screenshot::captureRegion(server.activeSceneOutputForTest(), server.renderer,
                                         { 100, 100, 0, 0 }, rw, rh);
  CHECK(rw == 0); CHECK(rh == 0);
  CHECK(empty.empty());
}
```

- [ ] **Step 2: Add the system test exe to `tests/meson.build`** (uses `text_env` — a red decorated window is in frame):

```meson
# Region screenshot: scene readback crop (texture_from_buffer + read_pixels).
screenshot_capture_exe = executable('screenshot-capture-test',
  files('system/screenshot_capture_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('screenshot_capture', screenshot_capture_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)
```

- [ ] **Step 3: Run it to verify it fails**

Run: `ninja -C build && meson test -C build screenshot_capture -v`
Expected: FAIL — the stub returns empty so `rw == 40` fails.

- [ ] **Step 4: Implement `captureRegion`** — replace the stub in `src/Screenshot.cc`

```cpp
  std::vector<uint32_t> captureRegion(wlr_scene_output *so, wlr_renderer *renderer,
                                      Rect sel, int &outW, int &outH) {
    outW = outH = 0;
    if (!so || !so->output || !renderer) return {};
    sel = clampToOutput(sel, so->output->width, so->output->height);
    if (sel.w <= 0 || sel.h <= 0) return {};

    wlr_output_state st;
    wlr_output_state_init(&st);
    if (!wlr_scene_output_build_state(so, &st, nullptr)) {
      wlr_output_state_finish(&st);
      return {};
    }
    wlr_texture *tex = wlr_texture_from_buffer(renderer, st.buffer);
    if (!tex) { wlr_output_state_finish(&st); return {}; }

    std::vector<uint32_t> px(static_cast<size_t>(sel.w) * sel.h);
    // src_box is a const member -> build the whole options aggregate at once.
    const wlr_texture_read_pixels_options opts = {
      .data = px.data(),
      .format = DRM_FORMAT_ARGB8888,
      .stride = static_cast<uint32_t>(sel.w) * 4,
      .dst_x = 0,
      .dst_y = 0,
      .src_box = { .x = sel.x, .y = sel.y, .width = sel.w, .height = sel.h },
    };
    const bool ok = wlr_texture_read_pixels(tex, &opts);
    wlr_texture_destroy(tex);
    wlr_output_state_finish(&st);
    if (!ok) return {};

    for (uint32_t &p : px) p |= 0xFF000000u;   // opaque guard (ARGB read is already opaque)
    outW = sel.w;
    outH = sel.h;
    return px;
  }
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `ninja -C build && meson test -C build screenshot_capture -v`
Expected: PASS (both cases).

- [ ] **Step 6: Commit**

```bash
git add src/Screenshot.cc tests/system/screenshot_capture_test.cc tests/meson.build
git commit -m "Screenshot: captureRegion scene readback (build_state + read_pixels crop)"
```

---

### Task 4: `ClipboardImage` server-side data source

**Files:**
- Create: `src/ClipboardImage.hh`, `src/ClipboardImage.cc`
- Modify: `src/meson.build` (add `ClipboardImage.cc` to `bbai_lib`)
- Test: `tests/system/clipboard_image_test.cc`
- Modify: `tests/meson.build`

**Interfaces:**
- Consumes: `encodePng` (Task 2) in the test only.
- Produces:
  - `struct bbai::ClipboardImage { wlr_data_source base; /* first member */ ... };`
  - `using Blob = std::shared_ptr<const std::vector<uint8_t>>;`
  - `ClipboardImage *ClipboardImage::create(wl_display *display, Blob png);` — inits base, registers `image/png`. Caller installs via `wlr_seat_set_selection(seat, &ci->base, serial)`. wlroots owns the lifetime thereafter.

- [ ] **Step 1: Write the failing test** — `tests/system/clipboard_image_test.cc`

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "Server.hh"
#include "ClipboardImage.hh"
#include "Screenshot.hh"

#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <cstring>
#include <memory>

using namespace bbai;

static ClipboardImage::Blob makeBlob() {
  std::vector<uint32_t> px(4 * 3, 0xFF112233u);
  return std::make_shared<const std::vector<uint8_t>>(screenshot::encodePng(px, 4, 3));
}

TEST_CASE("ClipboardImage offers image/png and serves the bytes over a pipe") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());

  auto blob = makeBlob();
  REQUIRE(blob->size() > 8);
  ClipboardImage *ci = ClipboardImage::create(server.display, blob);
  REQUIRE(ci != nullptr);

  // Exactly one offered mime type: image/png.
  REQUIRE(ci->base.mime_types.size / sizeof(char *) == 1);
  char **mt = static_cast<char **>(ci->base.mime_types.data);
  CHECK(std::strcmp(mt[0], "image/png") == 0);

  int fds[2];
  REQUIRE(pipe(fds) == 0);
  int rfd = fds[0], wfd = fds[1];
  fcntl(rfd, F_SETFL, fcntl(rfd, F_GETFL, 0) | O_NONBLOCK);

  // Drive the real vtable: send() takes ownership of wfd and registers the writer.
  ci->base.impl->send(&ci->base, "image/png", wfd);

  std::vector<uint8_t> got;
  for (int i = 0; i < 2000; ++i) {
    server.dispatch();
    uint8_t buf[4096];
    ssize_t n = read(rfd, buf, sizeof buf);
    if (n > 0) got.insert(got.end(), buf, buf + n);
    else if (n == 0) break;            // writer closed wfd -> EOF
    // n<0 EAGAIN: keep pumping
  }
  close(rfd);
  CHECK(got == *blob);
  CHECK(fcntl(wfd, F_GETFD) == -1);     // writer closed the write end

  // Destroy: wlroots would call impl->destroy on selection replacement; do it directly.
  ci->base.impl->destroy(&ci->base);    // no crash, frees the C++ subtype
}

TEST_CASE("ClipboardImage writer cleans up when the reader hangs up early") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());

  ClipboardImage *ci = ClipboardImage::create(server.display, makeBlob());
  int fds[2];
  REQUIRE(pipe(fds) == 0);
  close(fds[0]);                        // reader gone before any byte is read
  ci->base.impl->send(&ci->base, "image/png", fds[1]);
  for (int i = 0; i < 50; ++i) server.dispatch();   // writer hits EPIPE/HANGUP
  CHECK(fcntl(fds[1], F_GETFD) == -1);  // writer closed its fd, removed its source
  ci->base.impl->destroy(&ci->base);
}
```

- [ ] **Step 2: Add the system test exe to `tests/meson.build`**:

```meson
# Region screenshot: server-side image/png data source + async writer.
clipboard_image_exe = executable('clipboard-image-test',
  files('system/clipboard_image_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('clipboard_image', clipboard_image_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)
```

- [ ] **Step 3: Run it to verify it fails**

Run: `ninja -C build`
Expected: FAIL — `ClipboardImage.hh: No such file or directory`.

- [ ] **Step 4: Create the header** — `src/ClipboardImage.hh`

```cpp
// A server-owned wlr_data_source offering one image/png blob to the clipboard.
// Lifetime belongs to wlroots: install with wlr_seat_set_selection, and wlroots
// calls impl->destroy (synchronously, on selection replacement) to delete it.
// Do NOT wrap it in an owning smart pointer. The async paste writer holds its
// own ref to the (shared) bytes, so an in-flight paste survives a replacement.
#ifndef BLACKBOXAI_CLIPBOARD_IMAGE_HH
#define BLACKBOXAI_CLIPBOARD_IMAGE_HH

#include "wlr.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace bbai {

  struct ClipboardImage {
    wlr_data_source base;   // MUST be the first member (standard-layout, offset 0)
    wl_display *display = nullptr;
    std::shared_ptr<const std::vector<uint8_t>> png;

    using Blob = std::shared_ptr<const std::vector<uint8_t>>;

    // Allocate, wlr_data_source_init, and register the image/png mime type.
    static ClipboardImage *create(wl_display *display, Blob png);
  };

} // namespace bbai

#endif // BLACKBOXAI_CLIPBOARD_IMAGE_HH
```

- [ ] **Step 5: Implement** — `src/ClipboardImage.cc`

```cpp
#include "ClipboardImage.hh"

#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>

namespace bbai {

  namespace {
    // One in-flight paste: owns its own ref to the bytes + the write fd + source.
    struct Writer {
      ClipboardImage::Blob png;
      size_t offset = 0;
      int fd = -1;
      wl_event_source *src = nullptr;
    };

    void writerFinish(Writer *w) {
      if (w->src) wl_event_source_remove(w->src);   // remove BEFORE close
      if (w->fd >= 0) close(w->fd);
      delete w;
    }

    int writerCb(int fd, uint32_t mask, void *data) {
      auto *w = static_cast<Writer *>(data);
      if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) { writerFinish(w); return 0; }
      const std::vector<uint8_t> &bytes = *w->png;
      while (w->offset < bytes.size()) {
        ssize_t n = write(fd, bytes.data() + w->offset, bytes.size() - w->offset);
        if (n > 0) { w->offset += static_cast<size_t>(n); continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;  // wait writable
        writerFinish(w);                              // real write error (EPIPE etc.)
        return 0;
      }
      writerFinish(w);                                // all bytes written
      return 0;
    }

    void ciSend(wlr_data_source *source, const char *mime, int32_t fd) {
      auto *ci = reinterpret_cast<ClipboardImage *>(source);
      if (!mime || std::strcmp(mime, "image/png") != 0 || !ci->png) { close(fd); return; }
      const int flags = fcntl(fd, F_GETFL, 0);
      fcntl(fd, F_SETFL, (flags < 0 ? 0 : flags) | O_NONBLOCK);   // paste fd arrives BLOCKING
      auto *w = new Writer{ ci->png, 0, fd, nullptr };
      wl_event_loop *loop = wl_display_get_event_loop(ci->display);
      w->src = wl_event_loop_add_fd(loop, fd, WL_EVENT_WRITABLE, writerCb, w);
    }

    void ciDestroy(wlr_data_source *source) {
      // wlroots already freed base.mime_types + the strdup'd string before this.
      // We own only the C++ subtype; in-flight Writers keep their own blob ref.
      delete reinterpret_cast<ClipboardImage *>(source);
    }

    const wlr_data_source_impl kImpl = {
      .send = ciSend,
      .accept = nullptr,        // clipboard-only: accept/dnd_* are unused
      .destroy = ciDestroy,
      .dnd_drop = nullptr,
      .dnd_finish = nullptr,
      .dnd_action = nullptr,
    };
  } // namespace

  ClipboardImage *ClipboardImage::create(wl_display *display, Blob png) {
    auto *ci = new ClipboardImage();
    wlr_data_source_init(&ci->base, &kImpl);   // zero-fills base; set C++ members AFTER
    ci->display = display;
    ci->png = std::move(png);
    char **slot = static_cast<char **>(wl_array_add(&ci->base.mime_types, sizeof(char *)));
    if (slot) *slot = strdup("image/png");
    return ci;
  }

} // namespace bbai
```

- [ ] **Step 6: Add `ClipboardImage.cc` to `bbai_lib`** — in `src/meson.build`, add `'ClipboardImage.cc',` to the `files( ... )` list.

- [ ] **Step 7: Run the test to verify it passes**

Run: `ninja -C build && meson test -C build clipboard_image -v`
Expected: PASS (both cases).

- [ ] **Step 8: Commit**

```bash
git add src/ClipboardImage.hh src/ClipboardImage.cc src/meson.build \
        tests/system/clipboard_image_test.cc tests/meson.build
git commit -m "ClipboardImage: server-side image/png data source + async fd writer"
```

---

### Task 5: `Action::Screenshot` bound to Mod4+F7 arms the mode

**Files:**
- Modify: `src/Keybindings.hh` (enum), `src/Keybindings.cc` (binding)
- Modify: `src/Server.hh` (CursorMode value, `beginScreenshot`, `screenshotActiveForTest`), `src/Server.cc` (executeAction case, `beginScreenshot`)
- Test: `tests/unit/keybinding_test.cc` (pure dispatch), `tests/system/keybinding_test.cc` (inject → arms mode)

**Interfaces:**
- Produces: `Action::Screenshot`; `CursorMode::ScreenshotSelect`; `Server::beginScreenshot()`; `bool Server::screenshotActiveForTest() const`.

- [ ] **Step 1: Write the failing unit test** — append to `tests/unit/keybinding_test.cc`

```cpp
TEST_CASE("Mod4+F7 maps to Action::Screenshot, modifier-exact") {
  bbai::Keybindings kb;
  CHECK(kb.dispatch(WLR_MODIFIER_LOGO, XKB_KEY_F7).kind == bbai::Action::Screenshot);
  // Modifier EQUALITY: Mod4+Shift+F7 must NOT match.
  CHECK(kb.dispatch(WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT, XKB_KEY_F7).kind
        == bbai::Action::None);
}
```

- [ ] **Step 2: Write the failing system test** — append to `tests/system/keybinding_test.cc`

```cpp
TEST_CASE("Super+F7 fires Screenshot and arms the select mode") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  bbai::Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();
  CHECK_FALSE(server.screenshotActiveForTest());
  server.injectKeyForTest(XKB_KEY_F7, WLR_MODIFIER_LOGO, /*pressed=*/true);
  CHECK(server.lastActionForTest() == bbai::Action::Screenshot);
  CHECK(server.screenshotActiveForTest());
}
```

- [ ] **Step 3: Run to verify they fail**

Run: `ninja -C build`
Expected: FAIL — `Action::Screenshot` and `screenshotActiveForTest` do not exist (compile error).

- [ ] **Step 4: Add the action + binding**

In `src/Keybindings.hh`, add `Screenshot` to the `enum Kind`:
```cpp
    enum Kind {
      None, WorkspaceNext, WorkspacePrev, WorkspaceTo,
      OpenMenu, CloseWindow, CycleNext, CyclePrev,
      IconMenu, Screenshot
    };
```
In `src/Keybindings.cc`, add to the `bindings_` initializer list:
```cpp
      { SUPER,         XKB_KEY_F7,    { Action::Screenshot } },
```

- [ ] **Step 5: Add the mode + `beginScreenshot` + accessor**

In `src/Server.hh`: extend the enum and declare the members.
```cpp
    enum class CursorMode { Passthrough, Move, Resize, ScreenshotSelect };
```
Add to the public test-accessor group:
```cpp
    bool screenshotActiveForTest() const { return cursor_mode == CursorMode::ScreenshotSelect; }
```
Add to the private method declarations (near `beginInteractive`):
```cpp
    void beginScreenshot();   // arm region-select mode (crosshair); aborts any grab
```

In `src/Server.cc`: add the executeAction case and the method.
```cpp
    case Action::IconMenu:  openIconMenu(cursor->x, cursor->y); break;
    case Action::Screenshot: beginScreenshot(); break;   // <-- add this line
```
```cpp
  void Server::beginScreenshot() {
    // Abort any in-progress move/resize grab before arming (same as openRootMenu),
    // so the grab's terminating release can't strand the window.
    if (cursor_mode != CursorMode::Passthrough) {
      if (cursor_mode == CursorMode::Resize && grabbed_view)
        wlr_xdg_toplevel_set_resizing(grabbed_view->toplevel(), false);
      grabbed_view = nullptr;
      resize_edges = 0;
    }
    cursor_mode = CursorMode::ScreenshotSelect;
    wlr_cursor_set_xcursor(cursor, xcursor_mgr, "crosshair");
  }
```

- [ ] **Step 6: Run to verify they pass**

Run: `ninja -C build && meson test -C build unit keybinding -v`
Expected: PASS (both the unit `Mod4+F7` case and the system arm case).

- [ ] **Step 7: Commit**

```bash
git add src/Keybindings.hh src/Keybindings.cc src/Server.hh src/Server.cc \
        tests/unit/keybinding_test.cc tests/system/keybinding_test.cc
git commit -m "Keybindings: Action::Screenshot on Mod4+F7 arms ScreenshotSelect mode"
```

---

### Task 6: ScreenshotSelect state machine + GNOME dim overlay + Escape

**Files:**
- Modify: `src/Server.hh` (members + accessors + method decls), `src/Server.cc` (gates in `onPointerButton`/`onPointerMotion`/`onKey`/`injectKeyForTest`, overlay helpers, cancel)
- Test: `tests/system/screenshot_select_test.cc`
- Modify: `tests/meson.build`

**Interfaces:**
- Consumes: `beginScreenshot`/`CursorMode::ScreenshotSelect` (Task 5), `screenshot::fromCorners/clampToOutput/dimRects` (Task 1).
- Produces: `bool Server::screenshotOverlayActiveForTest() const`; internal `cancelScreenshot()`, `updateScreenshotOverlay()`, `destroyScreenshotOverlay()`. (Capture-on-release glue is Task 7; this task ends the drag by just exiting + tearing down the overlay.)

- [ ] **Step 1: Write the failing test** — `tests/system/screenshot_select_test.cc`

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>   // BTN_LEFT / BTN_RIGHT

using namespace bbai;

static Server *boot() {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  auto *s = new Server(/*headless=*/true);
  REQUIRE(s->ok());
  for (int i = 0; i < 50 && s->activeSceneOutputForTest() == nullptr; ++i) s->dispatch();
  return s;
}

TEST_CASE("arm, drag, release: overlay appears then is gone, mode exits") {
  Server *s = boot();
  s->injectKeyForTest(XKB_KEY_F7, WLR_MODIFIER_LOGO, true);
  REQUIRE(s->screenshotActiveForTest());
  CHECK_FALSE(s->screenshotOverlayActiveForTest());     // armed, not yet dragging

  s->injectPointerMotionForTest(200, 200);
  s->injectPointerButtonForTest(BTN_LEFT, true);        // anchor A
  CHECK(s->screenshotOverlayActiveForTest());
  s->injectPointerMotionForTest(400, 350);              // drag
  CHECK(s->screenshotOverlayActiveForTest());
  s->injectPointerButtonForTest(BTN_LEFT, false);       // release B (real drag)
  CHECK_FALSE(s->screenshotActiveForTest());            // back to passthrough
  CHECK_FALSE(s->screenshotOverlayActiveForTest());     // overlay torn down
  delete s;
}

TEST_CASE("Escape cancels the select mode") {
  Server *s = boot();
  s->injectKeyForTest(XKB_KEY_F7, WLR_MODIFIER_LOGO, true);
  s->injectPointerMotionForTest(200, 200);
  s->injectPointerButtonForTest(BTN_LEFT, true);
  REQUIRE(s->screenshotOverlayActiveForTest());
  s->injectKeyForTest(XKB_KEY_Escape, 0, true);
  CHECK_FALSE(s->screenshotActiveForTest());
  CHECK_FALSE(s->screenshotOverlayActiveForTest());
  delete s;
}

TEST_CASE("right-click cancels the select mode") {
  Server *s = boot();
  s->injectKeyForTest(XKB_KEY_F7, WLR_MODIFIER_LOGO, true);
  s->injectPointerMotionForTest(200, 200);
  s->injectPointerButtonForTest(BTN_LEFT, true);
  s->injectPointerButtonForTest(BTN_RIGHT, true);       // cancel
  CHECK_FALSE(s->screenshotActiveForTest());
  CHECK_FALSE(s->screenshotOverlayActiveForTest());
  delete s;
}

TEST_CASE("a sub-pixel drag is a cancel, not a capture") {
  Server *s = boot();
  s->injectKeyForTest(XKB_KEY_F7, WLR_MODIFIER_LOGO, true);
  s->injectPointerMotionForTest(200, 200);
  s->injectPointerButtonForTest(BTN_LEFT, true);
  s->injectPointerMotionForTest(201, 201);              // < 4px
  s->injectPointerButtonForTest(BTN_LEFT, false);
  CHECK_FALSE(s->screenshotActiveForTest());            // exited, no capture (Task 7 asserts no selection)
  CHECK_FALSE(s->screenshotOverlayActiveForTest());
  delete s;
}

TEST_CASE("while selecting, pointer input does not reach a focused client") {
  Server *s = boot();
  test::TestClient c(s->socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  REQUIRE(c.ok());
  auto mapped = [&] { const auto &v = s->viewsForTest(); return !v.empty() && v[0]->isMapped(); };
  for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); s->dispatch(); c.pump(); }
  s->injectKeyForTest(XKB_KEY_F7, WLR_MODIFIER_LOGO, true);
  s->injectPointerMotionForTest(260, 130);              // over the client
  s->injectPointerButtonForTest(BTN_LEFT, true);
  for (int i = 0; i < 10; ++i) { c.flush(); s->dispatch(); c.pump(); }
  CHECK(c.pointerButtonEvents() == 0);                  // modal: client saw nothing
  delete s;
}
```

- [ ] **Step 2: Add the test exe to `tests/meson.build`** (`text_env` — one case maps a red window):

```meson
# Region screenshot: ScreenshotSelect state machine + dim overlay + cancel paths.
screenshot_select_exe = executable('screenshot-select-test',
  files('system/screenshot_select_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('screenshot_select', screenshot_select_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)
```

- [ ] **Step 3: Run to verify it fails**

Run: `ninja -C build`
Expected: FAIL — `screenshotOverlayActiveForTest` undefined (compile error).

- [ ] **Step 4: Declare members + accessors** in `src/Server.hh`

Add to the public test-accessor group:
```cpp
    bool screenshotOverlayActiveForTest() const { return screenshot_overlay_ != nullptr; }
```
Add to the private method declarations:
```cpp
    void cancelScreenshot();           // tear down, restore cursor, no capture
    void updateScreenshotOverlay();    // reposition the four dim rects to the drag
    void destroyScreenshotOverlay();   // destroy the overlay tree
    void finishScreenshot();           // Task 7: capture on a real release (decl now)
```
Add to the private data members (near the grab state):
```cpp
    // ScreenshotSelect drag state + GNOME-dim overlay (layer_overlay).
    bool screenshot_dragging_ = false;
    int  screenshot_ax_ = 0, screenshot_ay_ = 0;     // anchor corner A
    wlr_scene_tree *screenshot_overlay_ = nullptr;
    wlr_scene_rect *screenshot_dim_[4] = { nullptr, nullptr, nullptr, nullptr };
```

- [ ] **Step 5: Implement the gates + helpers** in `src/Server.cc`

Add the overlay helpers (place them near `beginScreenshot`):
```cpp
  void Server::updateScreenshotOverlay() {
    if (!screenshot_overlay_) return;
    int ow = 1280, oh = 720;
    activeOutputSize(ow, oh);
    screenshot::Rect sel = screenshot::clampToOutput(
      screenshot::fromCorners(screenshot_ax_, screenshot_ay_,
                              static_cast<int>(cursor->x), static_cast<int>(cursor->y)),
      ow, oh);
    screenshot::DimRects d = screenshot::dimRects(ow, oh, sel);
    const screenshot::Rect *boxes[4] = { &d.above, &d.below, &d.left, &d.right };
    for (int i = 0; i < 4; ++i) {
      const screenshot::Rect &b = *boxes[i];
      if (b.w <= 0 || b.h <= 0) {
        wlr_scene_node_set_enabled(&screenshot_dim_[i]->node, false);
        continue;
      }
      wlr_scene_node_set_enabled(&screenshot_dim_[i]->node, true);
      wlr_scene_rect_set_size(screenshot_dim_[i], b.w, b.h);
      wlr_scene_node_set_position(&screenshot_dim_[i]->node, b.x, b.y);
    }
  }

  void Server::destroyScreenshotOverlay() {
    if (!screenshot_overlay_) return;
    wlr_scene_node_destroy(&screenshot_overlay_->node);   // destroys the rects too
    screenshot_overlay_ = nullptr;
    for (auto &r : screenshot_dim_) r = nullptr;
  }

  void Server::cancelScreenshot() {
    destroyScreenshotOverlay();
    screenshot_dragging_ = false;
    cursor_mode = CursorMode::Passthrough;
    wlr_cursor_set_xcursor(cursor, xcursor_mgr, "default");
  }
```

In `onPointerButton`, add the ScreenshotSelect gate immediately after the `active_menu_` gate (`if (active_menu_) { handleMenuButton(...); return; }`):
```cpp
    if (cursor_mode == CursorMode::ScreenshotSelect) {
      if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (button == BTN_RIGHT) { cancelScreenshot(); return; }   // right-click cancels
        if (button == BTN_LEFT && !screenshot_dragging_) {
          screenshot_dragging_ = true;
          screenshot_ax_ = static_cast<int>(cursor->x);
          screenshot_ay_ = static_cast<int>(cursor->y);
          screenshot_overlay_ = wlr_scene_tree_create(layer_overlay);
          const float dim[4] = { 0.f, 0.f, 0.f, 0.35f };   // premultiplied black
          for (auto &r : screenshot_dim_)
            r = wlr_scene_rect_create(screenshot_overlay_, 1, 1, dim);
          updateScreenshotOverlay();
        }
        return;
      }
      // RELEASED
      if (button == BTN_LEFT && screenshot_dragging_) finishScreenshot();
      return;   // own all releases while modal
    }
```

In `onPointerMotion`, add the gate before the toolbar edge-trigger line (`if (toolbar_) toolbar_->handlePointerMotion(...)`):
```cpp
    if (cursor_mode == CursorMode::ScreenshotSelect) {
      if (screenshot_dragging_) updateScreenshotOverlay();
      return;   // modal: no client/toolbar/grab handling while selecting
    }
```

In `onKey`, add the gate in the `WL_KEYBOARD_KEY_STATE_PRESSED` branch, right after the `active_menu_` block:
```cpp
      if (cursor_mode == CursorMode::ScreenshotSelect) {
        for (int i = 0; i < nsyms; ++i)
          if (syms[i] == XKB_KEY_Escape) { cancelScreenshot(); break; }
        swallowed_keycodes_.insert(keycode);   // swallow whatever key, incl. its release
        return;
      }
```

In `injectKeyForTest`, add the screenshot branch after the `active_menu_` branch:
```cpp
  void Server::injectKeyForTest(xkb_keysym_t sym, uint32_t mods, bool pressed) {
    if (active_menu_) { if (pressed) handleMenuKey(sym); return; }
    if (cursor_mode == CursorMode::ScreenshotSelect) {
      if (pressed && sym == XKB_KEY_Escape) cancelScreenshot();
      return;
    }
    if (pressed) dispatchBinding(mods, sym);
  }
```

Add a temporary `finishScreenshot` so this task compiles green (real capture lands in Task 7):
```cpp
  void Server::finishScreenshot() {
    // Task 7 replaces this body with capture -> encode -> clipboard.
    destroyScreenshotOverlay();
    screenshot_dragging_ = false;
    cursor_mode = CursorMode::Passthrough;
    wlr_cursor_set_xcursor(cursor, xcursor_mgr, "default");
  }
```

- [ ] **Step 6: Run to verify it passes**

Run: `ninja -C build && meson test -C build screenshot_select -v`
Expected: PASS (all five cases).

- [ ] **Step 7: Commit**

```bash
git add src/Server.hh src/Server.cc tests/system/screenshot_select_test.cc tests/meson.build
git commit -m "Server: ScreenshotSelect mode state machine + GNOME-dim overlay"
```

---

### Task 7: Release captures the region to the clipboard (end-to-end)

**Files:**
- Modify: `src/Server.hh` (a production scene-output getter + test accessors), `src/Server.cc` (real `finishScreenshot`)
- Test: `tests/system/screenshot_e2e_test.cc`
- Modify: `tests/meson.build`

**Interfaces:**
- Consumes: `screenshot::captureRegion` (Task 3), `screenshot::encodePng` (Task 2), `ClipboardImage::create` (Task 4), the overlay/drag state (Task 6).
- Produces: `wlr_scene_output *Server::activeSceneOutput() const` (production getter); `const char *Server::seatSelectionMimeForTest() const`; `wlr_data_source *Server::seatSelectionSourceForTest() const`.

- [ ] **Step 1: Write the failing test** — `tests/system/screenshot_e2e_test.cc`

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <cstring>
#include <linux/input-event-codes.h>
#include <png.h>

using namespace bbai;

static void mapOne(Server &s, test::TestClient &c) {
  auto mapped = [&] { const auto &v = s.viewsForTest(); return !v.empty() && v[0]->isMapped(); };
  for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); s.dispatch(); c.pump(); }
  for (int i = 0; i < 30; ++i) { c.flush(); s.dispatch(); c.pump(); }
}

TEST_CASE("Super+F7 drag-release puts an image/png of the region on the clipboard") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i) server.dispatch();

  test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  REQUIRE(c.ok());
  mapOne(server, c);
  View *v = server.viewsForTest()[0].get();
  const int x0 = v->x() + 1, y0 = v->y() + 23;   // inside client area, below titlebar

  server.injectKeyForTest(XKB_KEY_F7, WLR_MODIFIER_LOGO, true);
  server.injectPointerMotionForTest(x0 + 5, y0 + 5);
  server.injectPointerButtonForTest(BTN_LEFT, true);
  server.injectPointerMotionForTest(x0 + 85, y0 + 65);    // enclose ~80x60 of the window
  server.injectPointerButtonForTest(BTN_LEFT, false);

  CHECK_FALSE(server.screenshotActiveForTest());
  CHECK_FALSE(server.screenshotOverlayActiveForTest());
  REQUIRE(server.seatSelectionMimeForTest() != nullptr);
  CHECK(std::strcmp(server.seatSelectionMimeForTest(), "image/png") == 0);

  // Drive the owned source's writer over a pipe; assert it decodes to a PNG.
  wlr_data_source *src = server.seatSelectionSourceForTest();
  REQUIRE(src != nullptr);
  int fds[2]; REQUIRE(pipe(fds) == 0);
  fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
  src->impl->send(src, "image/png", fds[1]);
  std::vector<uint8_t> got;
  for (int i = 0; i < 2000; ++i) {
    server.dispatch();
    uint8_t buf[4096]; ssize_t n = read(fds[0], buf, sizeof buf);
    if (n > 0) got.insert(got.end(), buf, buf + n);
    else if (n == 0) break;
  }
  close(fds[0]);
  REQUIRE(got.size() > 8);
  CHECK(png_sig_cmp(got.data(), 0, 8) == 0);
}

TEST_CASE("a sub-pixel drag leaves the clipboard untouched") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i) server.dispatch();
  server.injectKeyForTest(XKB_KEY_F7, WLR_MODIFIER_LOGO, true);
  server.injectPointerMotionForTest(300, 300);
  server.injectPointerButtonForTest(BTN_LEFT, true);
  server.injectPointerMotionForTest(301, 301);
  server.injectPointerButtonForTest(BTN_LEFT, false);
  CHECK(server.seatSelectionSourceForTest() == nullptr);   // no selection set
}
```

- [ ] **Step 2: Add the test exe to `tests/meson.build`** (`text_env`):

```meson
# Region screenshot: end-to-end release -> capture -> clipboard.
screenshot_e2e_exe = executable('screenshot-e2e-test',
  files('system/screenshot_e2e_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('screenshot_e2e', screenshot_e2e_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)
```

- [ ] **Step 3: Run to verify it fails**

Run: `ninja -C build`
Expected: FAIL — `seatSelectionMimeForTest` / `seatSelectionSourceForTest` undefined.

- [ ] **Step 4: Add the getters** in `src/Server.hh`

Add a production scene-output getter and make the `_ForTest` one delegate (in the public accessor area):
```cpp
    wlr_scene_output *activeSceneOutput() const;     // production accessor
    wlr_scene_output *activeSceneOutputForTest() const { return activeSceneOutput(); }
```
(Replace the existing `activeSceneOutputForTest()` declaration with these two lines; move its body to `activeSceneOutput()` in the .cc.)

Add the selection test accessors:
```cpp
    const char *seatSelectionMimeForTest() const;
    wlr_data_source *seatSelectionSourceForTest() const { return seat->selection_source; }
```

Add the include for the new source type at the top of `src/Server.cc`:
```cpp
#include "ClipboardImage.hh"
#include "Screenshot.hh"
```

- [ ] **Step 5: Implement** in `src/Server.cc`

Provide `activeSceneOutput()` (rename the former `activeSceneOutputForTest` body):
```cpp
  wlr_scene_output *Server::activeSceneOutput() const {
    return active_output ? active_output->sceneOutput() : nullptr;
  }
```
Provide the mime accessor:
```cpp
  const char *Server::seatSelectionMimeForTest() const {
    wlr_data_source *s = seat->selection_source;
    if (!s || s->mime_types.size == 0) return nullptr;
    return *static_cast<char *const *>(s->mime_types.data);
  }
```
Replace the temporary `finishScreenshot` body with the real capture → clipboard:
```cpp
  void Server::finishScreenshot() {
    destroyScreenshotOverlay();                 // dim must be gone BEFORE the readback
    screenshot_dragging_ = false;
    cursor_mode = CursorMode::Passthrough;
    wlr_cursor_set_xcursor(cursor, xcursor_mgr, "default");

    int ow = 1280, oh = 720;
    activeOutputSize(ow, oh);
    screenshot::Rect sel = screenshot::clampToOutput(
      screenshot::fromCorners(screenshot_ax_, screenshot_ay_,
                              static_cast<int>(cursor->x), static_cast<int>(cursor->y)),
      ow, oh);
    if (sel.w < 4 || sel.h < 4) return;         // sub-pixel drag: treat as cancel

    int rw = 0, rh = 0;
    std::vector<uint32_t> px =
      screenshot::captureRegion(activeSceneOutput(), renderer, sel, rw, rh);
    if (px.empty()) return;                     // capture failed (e.g. GL read_pixels)
    std::vector<uint8_t> bytes = screenshot::encodePng(px, rw, rh);
    if (bytes.empty()) return;

    auto blob = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
    ClipboardImage *ci = ClipboardImage::create(display, blob);   // wlroots owns it
    wlr_seat_set_selection(seat, &ci->base, wl_display_next_serial(display));
  }
```

- [ ] **Step 6: Run to verify it passes**

Run: `ninja -C build && meson test -C build screenshot_e2e -v`
Expected: PASS (both cases).

- [ ] **Step 7: Full suite + coverage gate**

Run:
```sh
meson test -C build
gcovr -r . build --filter 'toolkit/' --filter 'src/' --fail-under-line=80
```
Expected: all suites green; coverage ≥ 80%.

- [ ] **Step 8: Commit**

```bash
git add src/Server.hh src/Server.cc tests/system/screenshot_e2e_test.cc tests/meson.build
git commit -m "Server: Super+F7 release captures region to clipboard end-to-end"
```

---

## Self-Review

**Spec coverage:**
- Trigger Mod4+F7 → mode → T5 (binding/arm), T6 (mode machine). ✓
- Press/drag/release/cancel (Escape, right-click, sub-px) → T6 + T7 (sub-px no-capture). ✓
- GNOME-style four-rect dim overlay, premultiplied, destroyed before capture → T6 (overlay), T7 (destroy-before-readback). ✓
- Renderer-agnostic capture (`build_state`→`texture_from_buffer`→`read_pixels(src_box)`) → T3. ✓
- In-memory PNG encode → T2. ✓
- Server-side `wlr_data_source` (image/png), async writer, refcounted blob, raw-pointer ownership → T4 (source/writer), T7 (set_selection, no owning pointer). ✓
- Crosshair cursor on entry, default on exit → T5 (set), T6/T7 (restore). ✓
- Non-goals (whole-window/screen, file-save, multi-monitor) → none implemented. ✓
- Modal input suppression → T6 (client-sees-nothing case). ✓

**Coverage-gap acknowledgements (verified by hand in the nested smoke run, not headless):** GL `read_pixels` + `GL_EXT_read_format_bgra`; live cross-client paste; the `crosshair` glyph actually present in the loaded theme. These are the items in the spec's "verified by hand" set — the review/smoke-test task owns them.

**Placeholder scan:** No "TBD"/"handle edge cases"/"similar to Task N" — every code step is complete. The two staged bodies (`captureRegion` stub in T2, `finishScreenshot` temp in T6) are explicitly replaced in T3/T7 with full code, not left as placeholders.

**Type consistency:** `Rect{x,y,w,h}` / `DimRects{above,below,left,right}` used identically T1→T3,T6,T7. `ClipboardImage::Blob = shared_ptr<const vector<uint8_t>>` consistent T4↔T7. `captureRegion(wlr_scene_output*, wlr_renderer*, Rect, int&, int&)` signature identical T2 decl / T3 impl / T7 call. `encodePng(vector<uint32_t>, int, int)` consistent T2↔T7. `screenshotActiveForTest`/`screenshotOverlayActiveForTest`/`seatSelectionMimeForTest`/`seatSelectionSourceForTest` named identically in decl and use.
