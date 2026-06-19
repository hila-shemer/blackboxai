// Reusable headless capture + golden-PNG compare. Boots a headless+pixman
// Server, drives the event loop manually (no free-running wl_display_run, so
// the process exits cleanly and gcov flushes), captures one composited frame
// in-process, and compares it to a golden PNG.
#ifndef BLACKBOXAI_HEADLESS_FIXTURE_HH
#define BLACKBOXAI_HEADLESS_FIXTURE_HH

#include <string>
#include <vector>
#include <cstdint>

namespace bbai { class Server; }

namespace bbai::test {

  // One captured frame, row-major ARGB8888 with alpha forced to 0xFF.
  struct Frame {
    uint32_t w = 0, h = 0;
    std::vector<uint32_t> pixels;
  };

  // Render and read back the current scene on `server`'s active output.
  // Throws std::runtime_error on failure.
  Frame captureFrame(bbai::Server &server);

  // Boot a headless Server, pump the loop until the single output composits,
  // and capture its frame. Throws std::runtime_error on any failure.
  Frame captureFirstFrame();

  // Compare `f` against the golden PNG at `golden_path`.
  //   tolerance    : max allowed abs per-channel (R/G/B) difference per pixel
  //   pixel_budget : max number of pixels allowed to exceed `tolerance`
  // With env BLESS set (and not "0"), (re)writes the golden and returns true.
  // On mismatch, writes <golden>-actual.png and <golden>-diff.png and returns
  // false.
  bool compareGolden(const Frame &f, const std::string &golden_path,
                     int tolerance = 2, int pixel_budget = 0);

  // Write `f` as a PNG to `path`, creating parent directories as needed.
  // Returns true on success.
  bool writeFramePng(const Frame &f, const std::string &path);

} // namespace bbai::test

#endif // BLACKBOXAI_HEADLESS_FIXTURE_HH
