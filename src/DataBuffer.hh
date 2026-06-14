// A minimal wlr_buffer backed by a heap ARGB8888 pixel array, modeled on
// labwc's lab_data_buffer. This is how Image::renderBuffer() output becomes a
// wlr_scene_buffer the compositor can scan out.
#ifndef BLACKBOXAI_DATABUFFER_HH
#define BLACKBOXAI_DATABUFFER_HH

#include "wlr.hpp"
#include <vector>
#include <cstdint>

namespace bbai {

  class DataBuffer {
  public:
    // Takes ownership of `pixels` (row-major ARGB8888, w*h entries). The buffer
    // self-destructs (delete) when its wlr_buffer refcount drops to zero.
    static DataBuffer *create(uint32_t w, uint32_t h, std::vector<uint32_t> pixels);

    wlr_buffer *base() { return &buffer; }
    uint32_t width() const { return w; }
    uint32_t height() const { return h; }

  private:
    // wlr_buffer_impl callbacks (static members so they can reach private state
    // via wl_container_of, while keeping the layout encapsulated).
    static DataBuffer *fromBase(wlr_buffer *b);
    static void implDestroy(wlr_buffer *b);
    static bool implBeginDataPtr(wlr_buffer *b, uint32_t flags, void **data,
                                 uint32_t *format, size_t *stride);
    static void implEndDataPtr(wlr_buffer *b);

    wlr_buffer buffer;
    std::vector<uint32_t> data;
    uint32_t w = 0, h = 0;
    static const wlr_buffer_impl impl;
  };

} // namespace bbai

#endif // BLACKBOXAI_DATABUFFER_HH
