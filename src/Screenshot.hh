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
