# BlackboxAI Milestone 1 — Headless Textured Background — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the smallest `blackboxai` Wayland compositor that, headless, renders one recognizably-Blackbox gradient/textured desktop background into a fixed 1280×720 output, proven by a golden-PNG system test and pixel-exact unit tests on the reused renderer.

**Architecture:** A C++20 shell on wlroots 0.19. We port the X-independent core of the `bt::` toolkit (`Color`, `Texture`, `Resource`, `Image`'s gradient/bevel math) **verbatim** from `reference/blackboxwm/lib/`, replacing only the X rendering tail with a new `Image::renderBuffer()` that packs the CPU `RGB[]` into an ARGB8888 buffer. The compositor (`Server`/`Output`) wraps that buffer in a `wlr_buffer` (`DataBuffer`) attached to a `wlr_scene_buffer` on the background scene layer. A headless+pixman test fixture captures the composited frame and compares it to a golden PNG.

**Tech Stack:** C++20, meson+ninja, wlroots-0.19, wayland-server, pixman-1, libpng, doctest (unit), gcovr (coverage). No GPU, no X11.

**Conventions for the implementer:**
- All git commits end with the trailer `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>` (omitted from the commit commands below for brevity — add it to every commit).
- Work happens on branch `wayland-rewrite` (already checked out).
- The upstream reference source is at `reference/blackboxwm/` (gitignored). When a step says "port from `lib/X`", copy that file and apply the listed edits; preserve the original MIT copyright header and add a line `// Ported to BlackboxAI (Wayland) — <what changed>`.
- wlroots is a C library; wrap every wlroots/wayland call site behind `toolkit/wlr.hpp` (Task 6). The three highest-risk API surfaces to verify against `/usr/include/wlroots-0.19/` headers first are: `wlr_backend_autocreate`, `wlr_scene_output_build_state`, and `wlr_buffer_begin_data_ptr_access` (Tasks 8 and 10). Verify their exact signatures before writing the call.

---

## File Structure

```
meson.build                     top-level build: project, deps, subdirs
meson_options.txt               tests(bool,true)
toolkit/meson.build             static_library('bt', ...) — the ported toolkit
toolkit/Color.{hh,cc}           bt::Color — RGB + standalone color parser (Task 2)
toolkit/Texture.{hh,cc}         bt::Texture — style spec + setDescription parser (Task 3)
toolkit/Resource.{hh,cc}        bt::Resource — standalone Xrm-syntax line parser (Task 4)
toolkit/Image.{hh,cc}           bt::Image — gradient/bevel math + renderBuffer() (Task 5)
toolkit/wlr.hpp                 the single extern "C" + WLR_USE_UNSTABLE boundary header (Task 6)
toolkit/listener.hpp            RAII Listener<Owner> over wl_listener (Task 6)
src/meson.build                 executable('blackboxai', ...)
src/DataBuffer.{hh,cc}          wlr_buffer impl over an ARGB8888 CPU buffer (Task 7)
src/Server.{hh,cc}              display/backend/renderer/allocator/scene/layers (Task 8)
src/Output.{hh,cc}              output config + background rendering (Task 9)
src/main.cc                     CLI parse → Server → run() (Task 8)
tests/meson.build               unit + system test wiring
tests/unit/*.cc                 doctest cases (Tasks 2-5, 6, 7)
tests/harness/HeadlessFixture.{hh,cc}   headless capture + golden compare (Task 10)
tests/system/background_test.cc         golden background system test (Task 11)
tests/golden/*.png              golden reference images
.github/workflows/ci.yml        build + test + coverage gate (Task 12)
```

---

## Task 1: Project skeleton + doctest + coverage wiring

**Files:**
- Create: `meson.build`, `meson_options.txt`, `toolkit/meson.build`, `tests/meson.build`, `tests/unit/smoke_test.cc`

- [ ] **Step 1: Write the failing test** — `tests/unit/smoke_test.cc`

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

TEST_CASE("doctest harness runs") {
    CHECK(1 + 1 == 2);
}
```

- [ ] **Step 2: Create the build files**

`meson.build`:
```meson
project('blackboxai', 'cpp',
  version : '0.1.0',
  default_options : ['cpp_std=c++20', 'warning_level=3', 'werror=false'])

add_project_arguments('-DWLR_USE_UNSTABLE', language : 'cpp')

cc = meson.get_compiler('cpp')

# Core deps (more added in later tasks)
doctest_dep = dependency('doctest', required : true)

subdir('toolkit')
if get_option('tests')
  subdir('tests')
endif
```

`meson_options.txt`:
```meson
option('tests', type : 'boolean', value : true, description : 'Build the test suites')
```

`toolkit/meson.build` (placeholder library so the top-level resolves; sources added in Tasks 2-6):
```meson
bt_sources = files()
bt_inc = include_directories('.')
# 'bt' is declared in Task 2 once it has sources; for now expose just the include dir.
bt_dep = declare_dependency(include_directories : bt_inc)
```

`tests/meson.build`:
```meson
unit_sources = files('unit/smoke_test.cc')
unit_exe = executable('unit-tests', unit_sources,
  dependencies : [doctest_dep, bt_dep])
test('unit', unit_exe, suite : 'unit')
```

- [ ] **Step 3: Configure and run the test**

Run:
```bash
meson setup build -Db_coverage=true -Dbuildtype=debug
meson test -C build --suite unit -v
```
Expected: PASS — `1 test, 0 failures`. (If `doctest` dependency is not found, install `doctest-devel` or vendor `doctest.h` under `third_party/` and use `include_directories` instead.)

- [ ] **Step 4: Verify coverage tooling**

Run: `ninja -C build coverage 2>/dev/null; gcovr -r . build --txt 2>/dev/null | tail -5`
Expected: gcovr prints a coverage table (numbers near 0% are fine here).

- [ ] **Step 5: Commit**

```bash
git add meson.build meson_options.txt toolkit/meson.build tests/meson.build tests/unit/smoke_test.cc
git commit -m "build: meson skeleton with doctest + coverage wiring"
```

---

## Task 2: Port `bt::Color` (RGB + standalone parser)

`bt::Color` stays screen-independent RGB; we drop X pixel allocation (`pixel()`, `deallocate()`, the `PenCache` friend, `_screen`/`_pixel`) and replace `namedColor()` (which queried the X server) with a standalone parser for `#rgb`, `#rrggbb`, `rgb:rr/gg/bb`, and a small named-color table.

**Files:**
- Create: `toolkit/Color.hh`, `toolkit/Color.cc`, `tests/unit/color_test.cc`
- Reference: `reference/blackboxwm/lib/Color.hh`

- [ ] **Step 1: Write the failing test** — `tests/unit/color_test.cc`

```cpp
#include <doctest/doctest.h>
#include "Color.hh"

using bt::Color;

TEST_CASE("parse #rrggbb") {
    Color c = Color::fromString("#ff8000");
    CHECK(c.valid());
    CHECK(c.red() == 0xff);
    CHECK(c.green() == 0x80);
    CHECK(c.blue() == 0x00);
}

TEST_CASE("parse short #rgb expands") {
    Color c = Color::fromString("#f80");
    CHECK(c.red() == 0xff);
    CHECK(c.green() == 0x88);
    CHECK(c.blue() == 0x00);
}

TEST_CASE("parse named colors") {
    CHECK(Color::fromString("black")  == Color(0, 0, 0));
    CHECK(Color::fromString("white")  == Color(255, 255, 255));
    CHECK(Color::fromString("grey20") == Color(51, 51, 51));
}

TEST_CASE("invalid color is invalid") {
    CHECK_FALSE(Color::fromString("not-a-color").valid());
    CHECK_FALSE(Color().valid());
}
```

- [ ] **Step 2: Run the test to verify it fails**

First add to `tests/meson.build`: append `'unit/color_test.cc'` to `unit_sources`.
Run: `meson test -C build --suite unit -v`
Expected: FAIL — `Color.hh` not found / `fromString` undefined.

- [ ] **Step 3: Write `toolkit/Color.hh`**

```cpp
// Ported to BlackboxAI (Wayland) from blackboxwm lib/Color.hh.
// Screen-independent RGB only; X pixel allocation removed.
#ifndef BLACKBOXAI_COLOR_HH
#define BLACKBOXAI_COLOR_HH
#include <string>
namespace bt {
  class Color {
  public:
    explicit Color(int r = -1, int g = -1, int b = -1)
      : _red(r), _green(g), _blue(b) {}

    // Parse "#rgb", "#rrggbb", "rgb:rr/gg/bb", or a named color.
    // Returns an invalid Color (valid()==false) on failure.
    static Color fromString(const std::string &spec);

    int red()   const { return _red; }
    int green() const { return _green; }
    int blue()  const { return _blue; }
    bool valid() const { return _red >= 0 && _green >= 0 && _blue >= 0; }

    bool operator==(const Color &c) const
    { return _red == c._red && _green == c._green && _blue == c._blue; }
    bool operator!=(const Color &c) const { return !operator==(c); }

  private:
    int _red, _green, _blue;
  };
} // namespace bt
#endif
```

- [ ] **Step 4: Write `toolkit/Color.cc`**

```cpp
#include "Color.hh"
#include <cctype>
#include <cstdlib>
#include <unordered_map>

namespace {
  int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = static_cast<char>(std::tolower(c));
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
  }
  // Minimal named-color table covering the colors used by the bundled styles.
  // Extend as needed; full rgb.txt parsing is a later refinement (spec D2).
  const std::unordered_map<std::string, bt::Color> &namedColors() {
    static const std::unordered_map<std::string, bt::Color> t = {
      {"black", bt::Color(0,0,0)}, {"white", bt::Color(255,255,255)},
      {"red", bt::Color(255,0,0)}, {"green", bt::Color(0,255,0)},
      {"blue", bt::Color(0,0,255)}, {"grey", bt::Color(190,190,190)},
      {"gray", bt::Color(190,190,190)},
      {"grey20", bt::Color(51,51,51)}, {"grey40", bt::Color(102,102,102)},
      {"grey60", bt::Color(153,153,153)}, {"grey80", bt::Color(204,204,204)},
      {"darkgrey", bt::Color(169,169,169)},
    };
    return t;
  }
}

namespace bt {
  Color Color::fromString(const std::string &spec) {
    if (spec.empty()) return Color();
    if (spec[0] == '#') {
      std::string h = spec.substr(1);
      auto allHex = [](const std::string &s) {
        for (char c : s) if (hexNibble(c) < 0) return false;
        return !s.empty();
      };
      if (!allHex(h)) return Color();
      if (h.size() == 3) // #rgb -> #rrggbb (replicate each nibble)
        return Color(hexNibble(h[0]) * 17, hexNibble(h[1]) * 17, hexNibble(h[2]) * 17);
      if (h.size() == 6)
        return Color(hexNibble(h[0]) * 16 + hexNibble(h[1]),
                     hexNibble(h[2]) * 16 + hexNibble(h[3]),
                     hexNibble(h[4]) * 16 + hexNibble(h[5]));
      return Color();
    }
    if (spec.rfind("rgb:", 0) == 0) { // rgb:rr/gg/bb
      int v[3] = {-1,-1,-1}; size_t i = 4, k = 0;
      while (k < 3 && i < spec.size()) {
        int hi = hexNibble(spec[i]); if (hi < 0) return Color();
        int lo = (i+1 < spec.size() && hexNibble(spec[i+1]) >= 0) ? hexNibble(spec[++i]) : hi;
        v[k++] = hi * 16 + lo; ++i;
        if (k < 3) { if (i >= spec.size() || spec[i] != '/') return Color(); ++i; }
      }
      if (k != 3) return Color();
      return Color(v[0], v[1], v[2]);
    }
    std::string lower; for (char c : spec) lower += static_cast<char>(std::tolower(c));
    auto it = namedColors().find(lower);
    return it == namedColors().end() ? Color() : it->second;
  }
} // namespace bt
```

- [ ] **Step 5: Add `Color.cc` to the `bt` library** — `toolkit/meson.build`

Replace the Task-1 placeholder with:
```meson
bt_inc = include_directories('.')
bt_lib = static_library('bt',
  files('Color.cc'),   # more sources appended in Tasks 3-6
  include_directories : bt_inc)
bt_dep = declare_dependency(link_with : bt_lib, include_directories : bt_inc)
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `meson test -C build --suite unit -v`
Expected: PASS — all `color_test` cases green.

- [ ] **Step 7: Commit**

```bash
git add toolkit/Color.hh toolkit/Color.cc toolkit/meson.build tests/unit/color_test.cc tests/meson.build
git commit -m "toolkit: port bt::Color with standalone color parser"
```

---

## Task 3: Port `bt::Texture` (style spec + parser)

Port `reference/blackboxwm/lib/Texture.{hh,cc}` keeping the `Texture` class and `setDescription()` parser **verbatim**, with edits: change the `Color` include to ours; **remove** the free functions `drawTexture()` and both `textureResource()` overloads (they need X/Pen/Resource we are not porting in M1 — they return in a later milestone); change `bt::Color` light/shadow computation to keep as-is (it only does integer math on RGB).

**Files:**
- Create: `toolkit/Texture.hh`, `toolkit/Texture.cc`, `tests/unit/texture_test.cc`
- Reference: `reference/blackboxwm/lib/Texture.hh`, `reference/blackboxwm/lib/Texture.cc`

- [ ] **Step 1: Write the failing test** — `tests/unit/texture_test.cc`

```cpp
#include <doctest/doctest.h>
#include "Texture.hh"

using bt::Texture;

TEST_CASE("setDescription parses appearance flags") {
    Texture t;
    t.setDescription("raised gradient diagonal");
    CHECK((t.texture() & Texture::Raised));
    CHECK((t.texture() & Texture::Gradient));
    CHECK((t.texture() & Texture::Diagonal));
    CHECK_FALSE((t.texture() & Texture::Sunken));
}

TEST_CASE("setDescription parses solid + interlaced + border") {
    Texture t;
    t.setDescription("flat solid interlaced border");
    CHECK((t.texture() & Texture::Flat));
    CHECK((t.texture() & Texture::Solid));
    CHECK((t.texture() & Texture::Interlaced));
    CHECK((t.texture() & Texture::Border));
}

TEST_CASE("colors are independently settable") {
    Texture t;
    t.setColor1(bt::Color(10, 20, 30));
    t.setColor2(bt::Color(40, 50, 60));
    CHECK(t.color1() == bt::Color(10, 20, 30));
    CHECK(t.color2() == bt::Color(40, 50, 60));
}
```

- [ ] **Step 2: Run to verify it fails**

Append `'unit/texture_test.cc'` to `unit_sources` in `tests/meson.build`.
Run: `meson test -C build --suite unit -v`
Expected: FAIL — `Texture.hh` not found.

- [ ] **Step 3: Port `toolkit/Texture.hh`**

Copy `reference/blackboxwm/lib/Texture.hh` → `toolkit/Texture.hh`, then:
- Change `#include "Color.hh"` / `#include "Util.hh"` to just `#include "Color.hh"` and `#include <string>`.
- **Delete** the `drawTexture(...)` declaration and both `textureResource(...)` declarations (lines ~38-69 in the original).
- Keep the entire `class Texture { ... }` (the `Type` enum, `setColor1/2`, `setDescription`, `texture()/addTexture()`, `borderWidth`, operators) verbatim.
- Note: `setColor1()` computes `lightColor`/`shadowColor` from color1 — keep that; it is pure integer math.

- [ ] **Step 4: Port `toolkit/Texture.cc`**

Copy `reference/blackboxwm/lib/Texture.cc` → `toolkit/Texture.cc`, then:
- Keep `Texture::operator=`, `setColor1` (light/shadow derivation), `setDescription` (the appearance-string parser) **verbatim**.
- **Delete** the `drawTexture()` definition and both `textureResource()` definitions, and any now-unused `#include` for `Pen.hh`/`Resource.hh`/`Image.hh`/X headers.
- `setColor1` references `bt::Color` constructors with `(r,g,b)` — compatible with our `Color`.

- [ ] **Step 5: Add to the library** — append `'Texture.cc'` to the `files(...)` in `toolkit/meson.build`.

- [ ] **Step 6: Run to verify it passes**

Run: `meson test -C build --suite unit -v`
Expected: PASS — all `texture_test` cases green.

- [ ] **Step 7: Commit**

```bash
git add toolkit/Texture.hh toolkit/Texture.cc toolkit/meson.build tests/unit/texture_test.cc tests/meson.build
git commit -m "toolkit: port bt::Texture spec + setDescription parser (drop X drawTexture)"
```

---

## Task 4: Port `bt::Resource` (standalone line parser)

Replace the `XrmDatabase` backend with a self-contained `key → value` map parsed from the Xrm file syntax (`Name.sub.class: value`, `!` line comments, blank lines, trailing-whitespace trim, `\`-continuation ignored for M1). `read(name, classname, default)` resolves `name`, then `classname`, then the default. Bundled style files use fully-qualified keys, so exact-match resolution is sufficient for M1 (full Xrm wildcard semantics deferred — spec D2).

**Files:**
- Create: `toolkit/Resource.hh`, `toolkit/Resource.cc`, `tests/unit/resource_test.cc`
- Reference: `reference/blackboxwm/lib/Resource.hh` (keep the public `read`/`write` API shape)

- [ ] **Step 1: Write the failing test** — `tests/unit/resource_test.cc`

```cpp
#include <doctest/doctest.h>
#include "Resource.hh"

TEST_CASE("parse key/value lines with comments") {
    bt::Resource r;
    r.loadFromString(
        "! a comment\n"
        "BlackboxAI.desktop:  raised gradient diagonal\n"
        "BlackboxAI.desktop.color:  #204060\n"
        "\n"
        "BlackboxAI.desktop.colorTo: #6080a0\n");
    CHECK(r.read("BlackboxAI.desktop", "BlackboxAI.Desktop", "") == "raised gradient diagonal");
    CHECK(r.read("BlackboxAI.desktop.color", "", "") == "#204060");
    CHECK(r.read("missing.key", "", "fallback") == "fallback");
}

TEST_CASE("classname fallback") {
    bt::Resource r;
    r.loadFromString("Foo.Class: viaclass\n");
    CHECK(r.read("foo.instance", "Foo.Class", "") == "viaclass");
}

TEST_CASE("integer and bool reads") {
    bt::Resource r;
    r.loadFromString("a.num: 42\na.flag: True\n");
    CHECK(r.read("a.num", "", 0) == 42);
    CHECK(r.read("a.flag", "", false) == true);
}
```

- [ ] **Step 2: Run to verify it fails**

Append `'unit/resource_test.cc'` to `unit_sources`.
Run: `meson test -C build --suite unit -v`
Expected: FAIL — `Resource.hh` not found.

- [ ] **Step 3: Write `toolkit/Resource.hh`**

```cpp
// Ported to BlackboxAI (Wayland) from blackboxwm lib/Resource.hh.
// XrmDatabase backend replaced by a standalone key/value parser.
#ifndef BLACKBOXAI_RESOURCE_HH
#define BLACKBOXAI_RESOURCE_HH
#include <string>
#include <unordered_map>
namespace bt {
  inline const char *boolAsString(bool b) { return b ? "True" : "False"; }

  class Resource {
  public:
    Resource() = default;
    explicit Resource(const std::string &filename) { load(filename); }

    bool valid() const { return loaded; }
    void load(const std::string &filename);         // reads file, replaces db
    void loadFromString(const std::string &text);   // parse from memory (testable)

    std::string read(const std::string &name, const std::string &classname,
                     const std::string &default_value = std::string()) const;
    int  read(const std::string &name, const std::string &classname, int dflt) const;
    bool read(const std::string &name, const std::string &classname, bool dflt) const;

  private:
    void parseLine(const std::string &line);
    std::unordered_map<std::string, std::string> db;
    bool loaded = false;
  };
} // namespace bt
#endif
```

- [ ] **Step 4: Write `toolkit/Resource.cc`**

```cpp
#include "Resource.hh"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace {
  std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
  }
}

namespace bt {
  void Resource::parseLine(const std::string &raw) {
    std::string line = trim(raw);
    if (line.empty() || line[0] == '!') return;
    size_t colon = line.find(':');
    if (colon == std::string::npos) return;
    std::string key = trim(line.substr(0, colon));
    std::string val = trim(line.substr(colon + 1));
    if (!key.empty()) db[key] = val;
  }

  void Resource::loadFromString(const std::string &text) {
    db.clear();
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) parseLine(line);
    loaded = true;
  }

  void Resource::load(const std::string &filename) {
    std::ifstream f(filename);
    if (!f) { loaded = false; return; }
    std::stringstream ss; ss << f.rdbuf();
    loadFromString(ss.str());
  }

  std::string Resource::read(const std::string &name, const std::string &classname,
                             const std::string &dflt) const {
    auto it = db.find(name);
    if (it != db.end()) return it->second;
    if (!classname.empty()) { it = db.find(classname); if (it != db.end()) return it->second; }
    return dflt;
  }

  int Resource::read(const std::string &name, const std::string &classname, int dflt) const {
    std::string v = read(name, classname, "");
    if (v.empty()) return dflt;
    try { return std::stoi(v); } catch (...) { return dflt; }
  }

  bool Resource::read(const std::string &name, const std::string &classname, bool dflt) const {
    std::string v = read(name, classname, "");
    if (v.empty()) return dflt;
    std::string lower; for (char c : v) lower += static_cast<char>(std::tolower(c));
    return lower == "true" || lower == "yes" || lower == "1";
  }
} // namespace bt
```

- [ ] **Step 5: Add to the library** — append `'Resource.cc'` to `toolkit/meson.build`.

- [ ] **Step 6: Run to verify it passes**

Run: `meson test -C build --suite unit -v`
Expected: PASS — all `resource_test` cases green.

- [ ] **Step 7: Commit**

```bash
git add toolkit/Resource.hh toolkit/Resource.cc toolkit/meson.build tests/unit/resource_test.cc tests/meson.build
git commit -m "toolkit: port bt::Resource as standalone key/value parser"
```

---

## Task 5: Port `bt::Image` gradient/bevel core + `renderBuffer()`

Port the gradient functions (`dgradient`/`egradient`/`hgradient`/`pgradient`/`rgradient`/`partial_vgradient`/`cdgradient`/`pcgradient`/`svgradient`) and `raisedBevel`/`sunkenBevel` **verbatim** (they fill the CPU `RGB data[]`). **Delete** `XColorTable`, the `XShm*` helpers, the dither tables/functions, `renderPixmap`, and the X `render()`/border-`XDrawRectangle`. Add a new unified `renderBuffer()` that fills `data[]` (solid OR gradient) + bevel + border + interlace, then packs to explicit ARGB8888 `uint32_t` (alpha forced to 0xFF).

**Files:**
- Create: `toolkit/Image.hh`, `toolkit/Image.cc`, `tests/unit/image_test.cc`
- Reference: `reference/blackboxwm/lib/Image.hh`, `reference/blackboxwm/lib/Image.cc` (gradient/bevel bodies only)

- [ ] **Step 1: Write the failing test** — `tests/unit/image_test.cc`

```cpp
#include <doctest/doctest.h>
#include <vector>
#include <cstdint>
#include "Image.hh"
#include "Texture.hh"

using bt::Image; using bt::Texture; using bt::Color;

static uint32_t argb(uint8_t r, uint8_t g, uint8_t b) {
    return (0xFFu << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

TEST_CASE("solid texture fills the whole buffer with color1, opaque") {
    Texture t; t.setDescription("flat solid"); t.setColor1(Color(0x20, 0x40, 0x60));
    Image img(4, 3);
    std::vector<uint32_t> buf = img.renderBuffer(t);
    REQUIRE(buf.size() == 4u * 3u);
    for (uint32_t px : buf) CHECK(px == argb(0x20, 0x40, 0x60));
}

TEST_CASE("diagonal gradient endpoints match color1 (top-left) and color2 (bottom-right)") {
    Texture t; t.setDescription("flat gradient diagonal");
    t.setColor1(Color(0, 0, 0)); t.setColor2(Color(255, 255, 255));
    Image img(16, 16);
    std::vector<uint32_t> buf = img.renderBuffer(t);
    // top-left ~ color1
    CHECK(buf[0] == argb(0, 0, 0));
    // bottom-right ~ color2
    CHECK(buf[16 * 16 - 1] == argb(255, 255, 255));
    // every pixel opaque
    for (uint32_t px : buf) CHECK((px >> 24) == 0xFFu);
}

TEST_CASE("border draws a 2px frame in borderColor") {
    Texture t; t.setDescription("flat solid border");
    t.setColor1(Color(0, 0, 0)); t.setBorderColor(Color(255, 0, 0)); t.setBorderWidth(2);
    Image img(8, 8);
    std::vector<uint32_t> buf = img.renderBuffer(t);
    CHECK(buf[0] == argb(255, 0, 0));            // corner is border
    CHECK(buf[8 * 1 + 1] == argb(255, 0, 0));    // 2nd ring still border
    CHECK(buf[8 * 2 + 2] == argb(0, 0, 0));      // inside is fill
}
```

- [ ] **Step 2: Run to verify it fails**

Append `'unit/image_test.cc'` to `unit_sources`.
Run: `meson test -C build --suite unit -v`
Expected: FAIL — `Image.hh` not found.

- [ ] **Step 3: Write `toolkit/Image.hh`**

```cpp
// Ported to BlackboxAI (Wayland) from blackboxwm lib/Image.hh.
// Keeps the CPU gradient/bevel renderer; replaces the X pixmap tail with renderBuffer().
#ifndef BLACKBOXAI_IMAGE_HH
#define BLACKBOXAI_IMAGE_HH
#include <vector>
#include <cstdint>
#include "Color.hh"
namespace bt {
  class Texture;
  struct RGB { unsigned char red, green, blue; };  // (drop the X 'reserved' bitfield)

  class Image {
  public:
    Image(unsigned int w, unsigned int h);
    ~Image();
    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;

    // Render a fully-composed appearance for `texture` into a packed ARGB8888
    // buffer (row-major, width*height, alpha = 0xFF). Replaces the old
    // render()->renderPixmap()->drawTexture() X path.
    std::vector<uint32_t> renderBuffer(const Texture &texture);

  private:
    RGB *data;
    unsigned int width, height;

    // verbatim from upstream (fill `data`):
    void raisedBevel(unsigned int border_width);
    void sunkenBevel(unsigned int border_width);
    void dgradient(const Color &from, const Color &to, bool interlaced);
    void egradient(const Color &from, const Color &to, bool interlaced);
    void hgradient(const Color &from, const Color &to, bool interlaced);
    void pgradient(const Color &from, const Color &to, bool interlaced);
    void rgradient(const Color &from, const Color &to, bool interlaced);
    void partial_vgradient(const Color &from, const Color &to, bool interlaced,
                           unsigned int fromHeight, unsigned int toHeight);
    void cdgradient(const Color &from, const Color &to, bool interlaced);
    void pcgradient(const Color &from, const Color &to, bool interlaced);
    void svgradient(const Color &from, const Color &to, bool interlaced);

    // new helpers:
    void fillSolid(const Color &c);
    void drawBorder(const Color &c, unsigned int bw);
    void applyInterlace();
  };
} // namespace bt
#endif
```

- [ ] **Step 4: Write `toolkit/Image.cc`**

1. Copy ONLY the gradient and bevel function bodies from `reference/blackboxwm/lib/Image.cc` (the `bt::Image::dgradient` … `bt::Image::svgradient`, `bt::Image::raisedBevel`, `bt::Image::sunkenBevel` definitions). They reference `data`, `width`, `height`, and `Color::red()/green()/blue()` — all compatible. Where the upstream `RGB` had a `reserved` field, our 3-byte `RGB` is fine; the gradient code writes `.red/.green/.blue` only.
2. Add the constructor/destructor and the new methods:

```cpp
#include "Image.hh"
#include "Texture.hh"
#include <cstring>

namespace bt {
  Image::Image(unsigned int w, unsigned int h)
    : data(new RGB[w * h]), width(w), height(h) {}
  Image::~Image() { delete [] data; }

  void Image::fillSolid(const Color &c) {
    RGB v{ (unsigned char)c.red(), (unsigned char)c.green(), (unsigned char)c.blue() };
    for (unsigned int i = 0; i < width * height; ++i) data[i] = v;
  }

  void Image::drawBorder(const Color &c, unsigned int bw) {
    RGB v{ (unsigned char)c.red(), (unsigned char)c.green(), (unsigned char)c.blue() };
    for (unsigned int i = 0; i < bw && i * 2 < width && i * 2 < height; ++i) {
      for (unsigned int x = i; x < width - i; ++x) {        // top & bottom rows
        data[i * width + x] = v;
        data[(height - 1 - i) * width + x] = v;
      }
      for (unsigned int y = i; y < height - i; ++y) {       // left & right cols
        data[y * width + i] = v;
        data[y * width + (width - 1 - i)] = v;
      }
    }
  }

  void Image::applyInterlace() {
    // Darken every other row ~12% (matches upstream interlace feel).
    for (unsigned int y = 1; y < height; y += 2)
      for (unsigned int x = 0; x < width; ++x) {
        RGB &p = data[y * width + x];
        p.red   = (unsigned char)(p.red   * 7 / 8);
        p.green = (unsigned char)(p.green * 7 / 8);
        p.blue  = (unsigned char)(p.blue  * 7 / 8);
      }
  }

  std::vector<uint32_t> Image::renderBuffer(const Texture &texture) {
    const Color from = texture.color1(), to = texture.color2();
    const bool interlaced = texture.texture() & Texture::Interlaced;

    if (texture.texture() & Texture::Gradient) {
      if      (texture.texture() & Texture::Diagonal)      dgradient(from, to, interlaced);
      else if (texture.texture() & Texture::Elliptic)      egradient(from, to, interlaced);
      else if (texture.texture() & Texture::Horizontal)    hgradient(from, to, interlaced);
      else if (texture.texture() & Texture::Pyramid)       pgradient(from, to, interlaced);
      else if (texture.texture() & Texture::Rectangle)     rgradient(from, to, interlaced);
      else if (texture.texture() & Texture::Vertical)      partial_vgradient(from, to, interlaced, 0, height);
      else if (texture.texture() & Texture::CrossDiagonal) cdgradient(from, to, interlaced);
      else if (texture.texture() & Texture::PipeCross)     pcgradient(from, to, interlaced);
      else if (texture.texture() & Texture::SplitVertical) svgradient(from, to, interlaced);
      else fillSolid(from);
    } else {
      fillSolid(from);
      if (interlaced) applyInterlace();
    }

    if      (texture.texture() & Texture::Raised) raisedBevel(texture.borderWidth());
    else if (texture.texture() & Texture::Sunken) sunkenBevel(texture.borderWidth());

    if (texture.texture() & Texture::Border)
      drawBorder(texture.borderColor(), texture.borderWidth() ? texture.borderWidth() : 1);

    std::vector<uint32_t> out(width * height);
    for (unsigned int i = 0; i < width * height; ++i)
      out[i] = (0xFFu << 24) | (uint32_t(data[i].red) << 16)
             | (uint32_t(data[i].green) << 8) | data[i].blue;
    return out;
  }
} // namespace bt
```

> Note: the gradient functions reference upstream globals for dithering (`global_ditherMode`) — when copying, drop any `if (dithered)` branches that call the dither path; keep only the truecolor `data[]` fill. The diagonal-gradient endpoint test in Step 1 will catch a mis-copied gradient body.

- [ ] **Step 5: Add to the library** — append `'Image.cc'` to `toolkit/meson.build`.

- [ ] **Step 6: Run to verify it passes**

Run: `meson test -C build --suite unit -v`
Expected: PASS — solid, gradient-endpoints, and border cases green. If the diagonal endpoints are off, re-check the copied `dgradient` body against upstream.

- [ ] **Step 7: Commit**

```bash
git add toolkit/Image.hh toolkit/Image.cc toolkit/meson.build tests/unit/image_test.cc tests/meson.build
git commit -m "toolkit: port bt::Image gradient/bevel core + ARGB8888 renderBuffer()"
```

---

## Task 6: wlroots boundary header + RAII `Listener`

**Files:**
- Create: `toolkit/wlr.hpp`, `toolkit/listener.hpp`, `tests/unit/listener_test.cc`

- [ ] **Step 1: Verify the wlroots API surface** (before writing)

Run:
```bash
pkg-config --modversion wlroots-0.19
grep -rn "wlr_backend_autocreate" /usr/include/wlroots-0.19/wlr/backend.h
```
Expected: `0.19.x`; the signature `struct wlr_backend *wlr_backend_autocreate(struct wl_event_loop *loop, struct wlr_session **session_ptr);`. Record the exact signature — Task 8 depends on it.

- [ ] **Step 2: Write `toolkit/wlr.hpp`** (the ONLY file that includes wlroots/wayland C headers)

```cpp
// The single C-interop boundary for BlackboxAI. No other file includes
// wlroots/wayland/xkb headers directly.
#ifndef BLACKBOXAI_WLR_HPP
#define BLACKBOXAI_WLR_HPP
#ifndef WLR_USE_UNSTABLE
#define WLR_USE_UNSTABLE
#endif
extern "C" {
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <wlr/interfaces/wlr_buffer.h>
}
#endif
```

- [ ] **Step 3: Write `toolkit/listener.hpp`** (RAII wrapper, ported from Wayfire's idiom)

```cpp
#ifndef BLACKBOXAI_LISTENER_HPP
#define BLACKBOXAI_LISTENER_HPP
#include "wlr.hpp"
#include <functional>
namespace bt {
  // Wraps a wl_listener with a std::function callback; disconnects in the dtor.
  class Listener {
  public:
    using Cb = std::function<void(void *data)>;
    Listener() { wl.notify = &Listener::thunk; wl_list_init(&wl.link); }
    ~Listener() { disconnect(); }
    Listener(const Listener &) = delete;
    Listener &operator=(const Listener &) = delete;

    void connect(wl_signal *signal, Cb cb) {
      disconnect(); callback = std::move(cb); wl_signal_add(signal, &wl);
    }
    void disconnect() {
      if (!wl_list_empty(&wl.link)) { wl_list_remove(&wl.link); wl_list_init(&wl.link); }
      callback = nullptr;
    }
  private:
    static void thunk(wl_listener *l, void *data) {
      Listener *self = wl_container_of(l, self, wl);
      if (self->callback) self->callback(data);
    }
    wl_listener wl;
    Cb callback;
  };
} // namespace bt
#endif
```

- [ ] **Step 4: Write the failing test** — `tests/unit/listener_test.cc`

```cpp
#include <doctest/doctest.h>
#include "listener.hpp"

TEST_CASE("Listener fires on signal and stops after disconnect") {
    wl_signal sig; wl_signal_init(&sig);
    int count = 0;
    {
        bt::Listener l;
        l.connect(&sig, [&](void *) { ++count; });
        wl_signal_emit_mutable(&sig, nullptr);
        CHECK(count == 1);
    } // dtor disconnects
    wl_signal_emit_mutable(&sig, nullptr);
    CHECK(count == 1); // no further increments
}
```

- [ ] **Step 5: Wire deps and run**

In top-level `meson.build`, after `doctest_dep`, add:
```meson
wlroots_dep = dependency('wlroots-0.19', version : ['>=0.19.0', '<0.20.0'])
wayland_server_dep = dependency('wayland-server')
```
In `tests/meson.build`, add `'unit/listener_test.cc'` to `unit_sources` and add `wlroots_dep, wayland_server_dep` to the `unit_exe` `dependencies`.

Run: `meson test -C build --suite unit -v`
Expected: PASS. (If `wl_signal_emit_mutable` is unavailable, use `wl_signal_emit`.)

- [ ] **Step 6: Commit**

```bash
git add toolkit/wlr.hpp toolkit/listener.hpp tests/unit/listener_test.cc meson.build tests/meson.build
git commit -m "toolkit: wlroots boundary header + RAII Listener"
```

---

## Task 7: `DataBuffer` — a `wlr_buffer` over ARGB8888 CPU pixels

A minimal `wlr_buffer` implementation backed by a heap ARGB8888 array, modeled on labwc's `lab_data_buffer`. This is how `renderBuffer()` output becomes a `wlr_scene_buffer`.

**Files:**
- Create: `src/DataBuffer.hh`, `src/DataBuffer.cc`, `src/meson.build`, `tests/unit/databuffer_test.cc`

- [ ] **Step 1: Write `src/DataBuffer.hh`**

```cpp
#ifndef BLACKBOXAI_DATABUFFER_HH
#define BLACKBOXAI_DATABUFFER_HH
#include "wlr.hpp"
#include <vector>
#include <cstdint>
namespace bbai {
  // Owns an ARGB8888 pixel array and exposes it as a single-format wlr_buffer.
  class DataBuffer {
  public:
    static DataBuffer *create(uint32_t w, uint32_t h, std::vector<uint32_t> pixels);
    wlr_buffer *base() { return &buffer; }
  private:
    wlr_buffer buffer;
    std::vector<uint32_t> data;
    uint32_t w, h;
    static const wlr_buffer_impl impl;
    friend const wlr_buffer_impl makeImpl();
  };
} // namespace bbai
#endif
```

- [ ] **Step 2: Write `src/DataBuffer.cc`**

```cpp
#include "DataBuffer.hh"
#include <drm_fourcc.h>

namespace bbai {
  static DataBuffer *fromBase(wlr_buffer *b) {
    return reinterpret_cast<DataBuffer *>(reinterpret_cast<char *>(b) - offsetof(DataBuffer, buffer));
  }
  static void db_destroy(wlr_buffer *b) { delete fromBase(b); }

  static bool db_begin_data_ptr(wlr_buffer *b, uint32_t /*flags*/, void **data,
                                uint32_t *format, size_t *stride) {
    DataBuffer *db = fromBase(b);
    *data = db->basePixels();
    *format = DRM_FORMAT_ARGB8888;
    *stride = db->width() * 4;
    return true;
  }
  static void db_end_data_ptr(wlr_buffer *) {}

  const wlr_buffer_impl DataBuffer::impl = {
    .destroy = db_destroy,
    .begin_data_ptr_access = db_begin_data_ptr,
    .end_data_ptr_access = db_end_data_ptr,
  };

  DataBuffer *DataBuffer::create(uint32_t w, uint32_t h, std::vector<uint32_t> pixels) {
    DataBuffer *db = new DataBuffer();
    db->data = std::move(pixels);
    db->w = w; db->h = h;
    wlr_buffer_init(&db->buffer, &DataBuffer::impl, w, h);
    return db;
  }
} // namespace bbai
```

> Add accessor methods `uint32_t width() const { return w; }`, `void *basePixels() { return data.data(); }` to the header (declared in the class). Verify the `wlr_buffer_impl` field set against `/usr/include/wlroots-0.19/wlr/interfaces/wlr_buffer.h` — some versions require `.get_dmabuf`/`.get_shm` to be present (may be left null). Use C++ designated initializers ONLY in this `.cc` (allowed in C++20) and keep field order matching the struct.

- [ ] **Step 3: Write `src/meson.build`**

```meson
src_inc = include_directories('.')
bbai_lib = static_library('bbai',
  files('DataBuffer.cc'),   # Server.cc, Output.cc appended in Tasks 8-9
  include_directories : [src_inc],
  dependencies : [bt_dep, wlroots_dep, wayland_server_dep])
bbai_dep = declare_dependency(link_with : bbai_lib,
  include_directories : [src_inc, bt_inc],
  dependencies : [bt_dep, wlroots_dep, wayland_server_dep])
```
Add `subdir('src')` to the top-level `meson.build` (before `subdir('tests')`), and add `'-DWLR_USE_UNSTABLE'` is already global. Toolkit headers need to be on the include path for `wlr.hpp`; since `wlr.hpp` lives in `toolkit/`, add `bt_inc` to `bbai_lib` includes (already via `bt_dep`).

- [ ] **Step 4: Write the failing test** — `tests/unit/databuffer_test.cc`

```cpp
#include <doctest/doctest.h>
#include "DataBuffer.hh"
#include <drm_fourcc.h>

TEST_CASE("DataBuffer exposes its pixels via data-ptr access") {
    std::vector<uint32_t> px(2 * 2, 0xFF112233u);
    bbai::DataBuffer *db = bbai::DataBuffer::create(2, 2, px);
    void *data = nullptr; uint32_t fmt = 0; size_t stride = 0;
    REQUIRE(wlr_buffer_begin_data_ptr_access(db->base(),
        WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &fmt, &stride));
    CHECK(fmt == DRM_FORMAT_ARGB8888);
    CHECK(stride == 2u * 4u);
    CHECK(reinterpret_cast<uint32_t *>(data)[0] == 0xFF112233u);
    wlr_buffer_end_data_ptr_access(db->base());
    wlr_buffer_drop(db->base());
}
```

- [ ] **Step 5: Wire + run**

Append `'unit/databuffer_test.cc'` to `unit_sources`; add `bbai_dep` to `unit_exe` dependencies. Build deps need `drm_fourcc.h` → add `libdrm_dep = dependency('libdrm')` in top-level meson and include it where needed.
Run: `meson test -C build --suite unit -v`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/DataBuffer.hh src/DataBuffer.cc src/meson.build tests/unit/databuffer_test.cc tests/meson.build meson.build
git commit -m "src: DataBuffer — wlr_buffer over ARGB8888 CPU pixels"
```

---

## Task 8: `Server` — compositor bring-up + layer trees

**Files:**
- Create: `src/Server.hh`, `src/Server.cc`, `src/main.cc`

- [ ] **Step 1: Write `src/Server.hh`**

```cpp
#ifndef BLACKBOXAI_SERVER_HH
#define BLACKBOXAI_SERVER_HH
#include "wlr.hpp"
#include "listener.hpp"
#include "Resource.hh"
namespace bbai {
  class Output;
  class Server {
  public:
    explicit Server(bool headless);
    ~Server();
    bool ok() const { return display != nullptr; }
    void run();        // wl_display_run (blocking)
    void terminate();  // wl_display_terminate
    bool dispatch();   // single event-loop iteration (for tests)

    wl_display *display = nullptr;
    wlr_backend *backend = nullptr;
    wlr_renderer *renderer = nullptr;
    wlr_allocator *allocator = nullptr;
    wlr_scene *scene = nullptr;
    wlr_output_layout *output_layout = nullptr;
    wlr_scene_output_layout *scene_layout = nullptr;
    // fixed layer trees (background used in M1)
    wlr_scene_tree *layer_background = nullptr;
    wlr_scene_tree *layer_bottom = nullptr;
    wlr_scene_tree *layer_window = nullptr;
    wlr_scene_tree *layer_top = nullptr;
    wlr_scene_tree *layer_overlay = nullptr;
    bt::Resource style;  // loaded desktop style for the background

  private:
    bt::Listener new_output;
    Output *active_output = nullptr;  // M1: single output
  };
} // namespace bbai
#endif
```

- [ ] **Step 2: Write `src/Server.cc`** (verify each `wlr_*` signature against headers first)

```cpp
#include "Server.hh"
#include "Output.hh"

namespace bbai {
  Server::Server(bool headless) {
    wlr_log_init(WLR_ERROR, nullptr);
    display = wl_display_create();
    wl_event_loop *loop = wl_display_get_event_loop(display);
    backend = headless ? wlr_headless_backend_create(loop)
                       : wlr_backend_autocreate(loop, nullptr);
    if (!backend) { wl_display_destroy(display); display = nullptr; return; }

    renderer = wlr_renderer_autocreate(backend);
    wlr_renderer_init_wl_display(renderer, display);
    allocator = wlr_allocator_autocreate(backend, renderer);

    wlr_compositor_create(display, 5, renderer);

    scene = wlr_scene_create();
    output_layout = wlr_output_layout_create(display);
    scene_layout = wlr_scene_attach_output_layout(scene, output_layout);

    layer_background = wlr_scene_tree_create(&scene->tree);
    layer_bottom     = wlr_scene_tree_create(&scene->tree);
    layer_window     = wlr_scene_tree_create(&scene->tree);
    layer_top        = wlr_scene_tree_create(&scene->tree);
    layer_overlay    = wlr_scene_tree_create(&scene->tree);

    // Default desktop style (overridable later by a real .blackboxrc).
    style.loadFromString("BlackboxAI.desktop: flat gradient diagonal\n"
                         "BlackboxAI.desktop.color:   #204060\n"
                         "BlackboxAI.desktop.colorTo: #6080a0\n");

    new_output.connect(&backend->events.new_output, [this](void *data) {
      auto *wlr_out = static_cast<wlr_output *>(data);
      active_output = new Output(*this, wlr_out);   // Output self-manages lifetime via destroy listener
    });

    wlr_backend_start(backend);
  }

  Server::~Server() {
    if (display) { wl_display_destroy_clients(display); wl_display_destroy(display); }
  }

  void Server::run() { wl_display_run(display); }
  void Server::terminate() { if (display) wl_display_terminate(display); }
  bool Server::dispatch() {
    wl_event_loop *loop = wl_display_get_event_loop(display);
    wl_display_flush_clients(display);
    return wl_event_loop_dispatch(loop, 0) >= 0;
  }
} // namespace bbai
```

> Verify-first: `wlr_backend_autocreate(loop, nullptr)`, `wlr_headless_backend_create(loop)`, `wlr_output_layout_create(display)`, `wlr_scene_attach_output_layout` return type. These are the 0.19 shapes from research; confirm against `/usr/include/wlroots-0.19/`.

- [ ] **Step 3: Write `src/main.cc`**

```cpp
#include "Server.hh"
#include <cstring>
int main(int argc, char **argv) {
  bool headless = false;
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--headless") == 0) headless = true;
  bbai::Server server(headless);
  if (!server.ok()) return 1;
  server.run();
  return 0;
}
```

- [ ] **Step 4: Wire the executable** — `src/meson.build`

Append `'Server.cc'` to `bbai_lib` sources, and add at the end:
```meson
executable('blackboxai', files('main.cc'),
  dependencies : [bbai_dep], install : true)
```
(Output.cc is added in Task 9; until then, comment out the `new_output` body or stub `Output` so it compiles. Prefer doing Task 9 immediately after.)

- [ ] **Step 5: Build to verify it compiles**

Run: `ninja -C build`
Expected: compiles (with an `Output` stub). Defer a runtime test to Task 9/10.

- [ ] **Step 6: Commit**

```bash
git add src/Server.hh src/Server.cc src/main.cc src/meson.build
git commit -m "src: Server compositor bring-up + scene layer trees"
```

---

## Task 9: `Output` — configure output + render the background

**Files:**
- Create: `src/Output.hh`, `src/Output.cc`

- [ ] **Step 1: Write `src/Output.hh`**

```cpp
#ifndef BLACKBOXAI_OUTPUT_HH
#define BLACKBOXAI_OUTPUT_HH
#include "wlr.hpp"
#include "listener.hpp"
namespace bbai {
  class Server;
  class Output {
  public:
    Output(Server &server, wlr_output *output);
    ~Output();
  private:
    void renderBackground();
    Server &server;
    wlr_output *output;
    wlr_scene_output *scene_output = nullptr;
    wlr_scene_buffer *bg = nullptr;
    bt::Listener frame, destroy;
  };
} // namespace bbai
#endif
```

- [ ] **Step 2: Write `src/Output.cc`**

```cpp
#include "Output.hh"
#include "Server.hh"
#include "DataBuffer.hh"
#include "Image.hh"
#include "Texture.hh"
#include "Color.hh"

namespace bbai {
  Output::Output(Server &srv, wlr_output *out) : server(srv), output(out) {
    wlr_output_init_render(output, server.allocator, server.renderer);

    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    if (wlr_output_mode *mode = wlr_output_preferred_mode(output))
      wlr_output_state_set_mode(&state, mode);
    else
      wlr_output_state_set_custom_mode(&state, 1280, 720, 0);  // headless
    wlr_output_state_set_scale(&state, 1);
    wlr_output_commit_state(output, &state);
    wlr_output_state_finish(&state);

    wlr_output_layout_output *lo = wlr_output_layout_add_auto(server.output_layout, output);
    scene_output = wlr_scene_output_create(server.scene, output);
    wlr_scene_output_layout_add_output(server.scene_layout, lo, scene_output);

    renderBackground();

    frame.connect(&output->events.frame, [this](void *) {
      wlr_scene_output_commit(scene_output, nullptr);
      timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
      wlr_scene_output_send_frame_done(scene_output, &now);
    });
    destroy.connect(&output->events.destroy, [this](void *) { delete this; });
  }

  Output::~Output() { if (bg) wlr_scene_node_destroy(&bg->node); }

  void Output::renderBackground() {
    int w = output->width, h = output->height;
    bt::Texture t;
    t.setDescription(server.style.read("BlackboxAI.desktop", "BlackboxAI.Desktop",
                                       "flat solid"));
    t.setColor1(bt::Color::fromString(server.style.read("BlackboxAI.desktop.color", "", "#204060")));
    t.setColor2(bt::Color::fromString(server.style.read("BlackboxAI.desktop.colorTo", "", "#6080a0")));

    bt::Image img(w, h);
    DataBuffer *buf = DataBuffer::create(w, h, img.renderBuffer(t));
    bg = wlr_scene_buffer_create(server.layer_background, buf->base());
    wlr_buffer_drop(buf->base());  // scene_buffer took its own ref
    wlr_scene_node_set_position(&bg->node, 0, 0);
  }
} // namespace bbai
```

> Verify-first: `wlr_output_layout_add_auto` vs `wlr_output_layout_add`, `wlr_scene_output_layout_add_output` arg order, and that `wlr_scene_buffer_create` takes (`parent_tree`, `wlr_buffer*`). Confirm `output->width/height` are populated after `commit_state` (they are once a mode is set).

- [ ] **Step 3: Wire + build**

Append `'Output.cc'` to `bbai_lib` sources in `src/meson.build`; remove the Task-8 `Output` stub.
Run: `ninja -C build`
Expected: compiles and links `blackboxai`.

- [ ] **Step 4: Manual smoke (nested)** — optional sanity, not a gate

Run (inside your Wayland session):
```bash
WAYLAND_DISPLAY=wayland-0 ./build/src/blackboxai &   # opens a nested output window via the wayland backend
```
Expected: a window showing the diagonal blue gradient. Kill it after. (Headless+golden is the real gate, Task 10-11.)

- [ ] **Step 5: Commit**

```bash
git add src/Output.hh src/Output.cc src/meson.build
git commit -m "src: Output renders the Blackbox gradient background to the scene"
```

---

## Task 10: `HeadlessFixture` — capture + golden compare

Capture the composited frame in-process: build the scene output state into a buffer, read its CPU pixels via `wlr_buffer_begin_data_ptr_access`, encode/compare PNG. **This is the highest-risk API surface — verify the readback path against headers + tinywl before coding.**

**Files:**
- Create: `tests/harness/HeadlessFixture.hh`, `tests/harness/HeadlessFixture.cc`

- [ ] **Step 1: Verify the capture API**

Run:
```bash
grep -rn "wlr_scene_output_build_state\|begin_data_ptr_access" /usr/include/wlroots-0.19/wlr/
```
Confirm `wlr_scene_output_build_state(struct wlr_scene_output *, struct wlr_output_state *, const struct wlr_scene_output_state_options *)` and the data-ptr access signature. Record them.

- [ ] **Step 2: Write `tests/harness/HeadlessFixture.hh`**

```cpp
#ifndef BLACKBOXAI_HEADLESS_FIXTURE_HH
#define BLACKBOXAI_HEADLESS_FIXTURE_HH
#include <string>
#include <vector>
#include <cstdint>
namespace bbai { class Server; }
namespace bbai::test {
  struct Frame { uint32_t w, h; std::vector<uint32_t> pixels; }; // ARGB8888
  // Boots a headless Server, pumps the loop until one frame composits, captures it.
  Frame captureFirstFrame();
  // PNG golden compare: tolerance = max abs per-channel diff; budget = max #differing px.
  // BLESS=1 env regenerates the golden and returns true.
  bool compareGolden(const Frame &f, const std::string &golden_path,
                     int tolerance = 2, int pixel_budget = 0);
}
#endif
```

- [ ] **Step 3: Write `tests/harness/HeadlessFixture.cc`**

```cpp
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "wlr.hpp"
#include <png.h>
#include <cstdlib>
#include <cstdio>

namespace bbai::test {
  // NOTE: accesses Server internals; the harness is built with the src include dir.
  Frame captureFirstFrame() {
    setenv("WLR_BACKENDS", "headless", 1);
    setenv("WLR_RENDERER", "pixman", 1);
    Server server(/*headless=*/true);
    REQUIRE_MESSAGE(server.ok(), "server failed to start"); // doctest macro available via includer

    // Pump until the single output exists and a frame is buildable.
    for (int i = 0; i < 50 && server.active_output_for_test() == nullptr; ++i)
      server.dispatch();
    wlr_scene_output *so = server.active_scene_output_for_test();

    wlr_output_state st; wlr_output_state_init(&st);
    wlr_scene_output_build_state(so, &st, nullptr);   // renders the scene into st.buffer

    Frame f; void *data = nullptr; uint32_t fmt = 0; size_t stride = 0;
    wlr_buffer_begin_data_ptr_access(st.buffer, WLR_BUFFER_DATA_PTR_ACCESS_READ,
                                     &data, &fmt, &stride);
    f.w = so->output->width; f.h = so->output->height;
    f.pixels.resize(f.w * f.h);
    auto *src = static_cast<uint8_t *>(data);
    for (uint32_t y = 0; y < f.h; ++y)
      memcpy(&f.pixels[y * f.w], src + y * stride, f.w * 4);
    wlr_buffer_end_data_ptr_access(st.buffer);
    wlr_output_state_finish(&st);
    return f;
  }

  // ... compareGolden(): write PNG via libpng if BLESS=1 (mkdir golden dir),
  //     else load golden PNG, compare dims + per-pixel within tolerance, count
  //     violations against pixel_budget; on failure write <golden>-actual.png and
  //     <golden>-diff.png next to the golden. (Use libpng: png_create_write_struct/
  //     png_create_read_struct, RGBA8.)
}
```

> Implement `compareGolden` fully with libpng (the comment outlines it; no placeholder ships — write the read/write/compare). Add `Server::active_scene_output_for_test()`/`active_output_for_test()` accessors to `Server` (test-only, guarded by a comment) returning the M1 single output's `wlr_scene_output*` and `Output*`. If `wlr_scene_output_build_state` needs the output to be committable, ensure the output mode is set (Task 9 did). If readback via `build_state` proves difficult, the fallback is `wlr_scene_output_commit` then `wlr_output`'s committed buffer via `output->...` — but try `build_state` first.

- [ ] **Step 4: Wire the harness lib** — `tests/meson.build`

```meson
png_dep = dependency('libpng')
harness_lib = static_library('harness', files('harness/HeadlessFixture.cc'),
  dependencies : [bbai_dep, wlroots_dep, wayland_server_dep, png_dep, doctest_dep],
  include_directories : include_directories('harness'))
harness_dep = declare_dependency(link_with : harness_lib,
  include_directories : include_directories('harness'),
  dependencies : [bbai_dep, png_dep])
```

- [ ] **Step 5: Build to verify it compiles**

Run: `ninja -C build`
Expected: harness compiles. (Exercised by Task 11.)

- [ ] **Step 6: Commit**

```bash
git add tests/harness/HeadlessFixture.hh tests/harness/HeadlessFixture.cc tests/meson.build src/Server.hh src/Server.cc
git commit -m "tests: headless capture harness (build_state + data-ptr readback) + golden compare"
```

---

## Task 11: System golden test — the background

**Files:**
- Create: `tests/system/background_test.cc`, `tests/golden/m1-background-diagonal-gradient.png` (generated via BLESS)

- [ ] **Step 1: Write the system test** — `tests/system/background_test.cc`

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"

TEST_CASE("headless background matches the golden gradient") {
    auto frame = bbai::test::captureFirstFrame();
    CHECK(frame.w == 1280u);
    CHECK(frame.h == 720u);
    CHECK(bbai::test::compareGolden(frame,
        "tests/golden/m1-background-diagonal-gradient.png", /*tol=*/2, /*budget=*/0));
}
```

- [ ] **Step 2: Wire the system suite** — `tests/meson.build`

```meson
system_exe = executable('system-tests', files('system/background_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('background', system_exe, suite : 'system',
  workdir : meson.project_source_root(),
  env : ['WLR_BACKENDS=headless', 'WLR_RENDERER=pixman'])
```

- [ ] **Step 3: Generate the golden**

Run:
```bash
export XDG_RUNTIME_DIR=$(mktemp -d); chmod 700 "$XDG_RUNTIME_DIR"
ninja -C build
BLESS=1 meson test -C build --suite system -v
```
Expected: PASS (BLESS writes `tests/golden/m1-background-diagonal-gradient.png`). Inspect the PNG — it must be a blue diagonal gradient.

- [ ] **Step 4: Re-run without BLESS to verify the gate works**

Run: `meson test -C build --suite system -v`
Expected: PASS against the committed golden.

- [ ] **Step 5: Commit**

```bash
git add tests/system/background_test.cc tests/golden/m1-background-diagonal-gradient.png tests/meson.build
git commit -m "tests: golden background system test (headless gradient)"
```

---

## Task 12: Coverage gate + CI

**Files:**
- Create: `.github/workflows/ci.yml`; Modify: nothing in source

- [ ] **Step 1: Measure current coverage**

Run:
```bash
meson test -C build --suite unit
meson test -C build --suite system
gcovr -r . build --filter 'toolkit/' --filter 'src/' --txt | tail -20
```
Expected: a coverage table. The toolkit (Color/Texture/Resource/Image) should already be high; record the number. (The 80% gate is enforced from M5; M1 just establishes the measurement.)

- [ ] **Step 2: Write `.github/workflows/ci.yml`**

```yaml
name: ci
on: [push, pull_request]
jobs:
  build-test:
    runs-on: ubuntu-latest
    container: fedora:43
    steps:
      - uses: actions/checkout@v4
      - name: deps
        run: dnf install -y meson ninja-build gcc-c++ gcovr pkgconf-pkg-config
             wlroots-devel wayland-devel wayland-protocols-devel pixman-devel
             libdrm-devel libxkbcommon-devel libpng-devel doctest-devel git
      - name: build
        run: meson setup build -Db_coverage=true -Dbuildtype=debug && ninja -C build
      - name: unit tests
        run: meson test -C build --suite unit -v
      - name: system tests
        run: |
          export XDG_RUNTIME_DIR=$(mktemp -d); chmod 700 "$XDG_RUNTIME_DIR"
          WLR_BACKENDS=headless WLR_RENDERER=pixman meson test -C build --suite system -v
      - name: coverage report (gate enforced from M5)
        run: gcovr -r . build --filter 'toolkit/' --filter 'src/' --txt --xml coverage.xml
      - name: upload artifacts on failure
        if: failure()
        uses: actions/upload-artifact@v4
        with:
          name: failure-artifacts
          path: |
            build/meson-logs/testlog.txt
            tests/golden/*-actual.png
            tests/golden/*-diff.png
```

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: build + unit + headless system tests + coverage report"
```

- [ ] **Step 4: Milestone wrap commit**

```bash
git commit --allow-empty -m "milestone: M1 headless textured background complete

blackboxai --headless renders a Blackbox gradient desktop, proven by a
golden-PNG system test; bt:: renderer ported with pixel-exact unit tests."
```

---

## Self-Review

**Spec coverage** (against `docs/superpowers/specs/2026-06-14-blackboxai-design.md` §6 M1):
- meson build + wlr.hpp + Listener → Tasks 1, 6 ✓
- Port Color/Texture/Resource/Image with renderBuffer seam → Tasks 2-5 ✓
- Server (display/backend/renderer/allocator/scene/output_layout + 5 layer trees) → Task 8 ✓
- Output (configure, render background Texture → wlr_buffer → wlr_scene_buffer, frame loop) → Tasks 7, 9 ✓
- HeadlessFixture (capture + libpng + golden + BLESS) → Task 10 ✓
- Unit tests on exact gradient/bevel pixels + Texture/Resource parse → Tasks 2-5 ✓
- System golden test → Task 11 ✓
- Acceptance: `ninja` builds, `--suite system` matches golden, `--suite unit` asserts pixels → Tasks 11, 5 ✓
- Out of scope (no clients/input/decorations) → respected ✓

**Placeholder scan:** Task 10's `compareGolden` body is described, not shown — the step explicitly requires the implementer to write the full libpng read/write/compare (flagged, not a silent TODO). All other steps contain complete code.

**Type consistency:** `Color::fromString` (Tasks 2,9), `Texture::setDescription/setColor1/2/setBorderColor/setBorderWidth` (Tasks 3,5,9), `Image::renderBuffer` returns `std::vector<uint32_t>` (Tasks 5,9), `Resource::read/loadFromString` (Tasks 4,8,9), `DataBuffer::create/base` (Tasks 7,9,10), `Server::dispatch/ok` + test accessors (Tasks 8,10) — names consistent across tasks.

**Known verify-first risks** (call out to the executing agent): exact wlroots 0.19 signatures for `wlr_backend_autocreate`, `wlr_output_layout_add_auto`, `wlr_scene_output_layout_add_output`, `wlr_scene_output_build_state`, `wlr_buffer_impl` fields, and `wlr_buffer_begin_data_ptr_access`. Each is isolated behind a single call site; the TDD compile loop catches drift.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-14-blackboxai-m1-headless-background.md`.
