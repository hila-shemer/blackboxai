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
