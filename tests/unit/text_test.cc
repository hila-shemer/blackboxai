// T3: bt::TextRenderer determinism. Runs under an ISOLATED fontconfig
// (FONTCONFIG_FILE + workdir set by meson to tests/fixtures/fonts.conf, whose
// single <dir> holds only the bundled LiberationMono-Regular.ttf), so every
// family resolves to that one font and the raster is host-independent. The
// pinned metrics + the a8-blend checksum are the authoritative regression signal
// for "did text rendering change" — the frame golden (T4) inherits the same
// isolation and can therefore stay strict.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "Text.hh"
#include "Color.hh"

#include <cstdint>
#include <vector>

namespace {
  // The pinned font is LiberationMono at pixelsize=16 (height 18 <= the 19px
  // label rect). Metrics verified deterministic across repeated isolated runs.
  constexpr int kPixelSize = 16;
  constexpr int kAscent    = 14;
  constexpr int kDescent   = 5;
  constexpr int kHeight    = 18;
  constexpr int kAg1Width  = 30;  // 3 monospace glyphs * advance 10

  uint64_t fnv1a(const std::vector<uint32_t> &buf) {
    uint64_t h = 1469598103934665603ULL;
    for (uint32_t px : buf) {
      for (int s = 0; s < 32; s += 8) {
        h ^= (px >> s) & 0xFF;
        h *= 1099511628211ULL;
      }
    }
    return h;
  }
}

TEST_CASE("TextRenderer loads the bundled font with pinned metrics") {
  bt::TextRenderer tr("monospace", kPixelSize);
  REQUIRE(tr.ok());
  CHECK(tr.ascent()  == kAscent);
  CHECK(tr.descent() == kDescent);
  CHECK(tr.height()  == kHeight);
  CHECK(tr.textWidth(U"Ag1") == kAg1Width);
}

TEST_CASE("drawText blends a deterministic, byte-stable raster") {
  bt::TextRenderer tr("monospace", kPixelSize);
  REQUIRE(tr.ok());

  // White 40x19 background; draw black "Ag1" with baseline at the ascent.
  const int W = 40, H = 19;
  std::vector<uint32_t> buf(static_cast<size_t>(W) * H, 0xFFFFFFFFu);
  const int penEnd =
    tr.drawText(buf, W, H, /*penX=*/0, /*baselineY=*/kAscent, U"Ag1", bt::Color(0, 0, 0));
  CHECK(penEnd == kAg1Width);

  // Text must have actually marked some pixels (not all still white).
  size_t nonWhite = 0;
  for (uint32_t px : buf) if ((px & 0x00FFFFFFu) != 0x00FFFFFFu) ++nonWhite;
  CHECK(nonWhite > 0);

  // Authoritative pin: the exact blended raster of black "Ag1" at pixelsize=16
  // over white. Stable across isolated runs; re-bless only on a deliberate
  // freetype/font change.
  constexpr uint64_t kAg1Checksum = 7738476215517073716ULL;
  CHECK(fnv1a(buf) == kAg1Checksum);
}

TEST_CASE("drawText edge cases: empty string and full clip") {
  bt::TextRenderer tr("monospace", kPixelSize);
  REQUIRE(tr.ok());
  const int W = 16, H = 19;
  std::vector<uint32_t> buf(static_cast<size_t>(W) * H, 0xFF112233u);
  const auto pristine = buf;

  // Empty string draws nothing and returns the pen unchanged.
  CHECK(tr.drawText(buf, W, H, 5, kAscent, U"", bt::Color(0, 0, 0)) == 5);
  CHECK(buf == pristine);

  // Pen well off the right edge: nothing drawn, no out-of-bounds access.
  tr.drawText(buf, W, H, 1000, kAscent, U"Ag1", bt::Color(0, 0, 0));
  CHECK(buf == pristine);

  // Negative baseline far above the buffer: also fully clipped.
  tr.drawText(buf, W, H, 0, -1000, U"Ag1", bt::Color(0, 0, 0));
  CHECK(buf == pristine);
}
