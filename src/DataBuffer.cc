#include "DataBuffer.hh"

#include <drm_fourcc.h>

namespace bbai {

  DataBuffer *DataBuffer::fromBase(wlr_buffer *b) {
    DataBuffer *db;
    db = wl_container_of(b, db, buffer);
    return db;
  }

  void DataBuffer::implDestroy(wlr_buffer *b) { delete fromBase(b); }

  bool DataBuffer::implBeginDataPtr(wlr_buffer *b, uint32_t /*flags*/, void **data,
                                    uint32_t *format, size_t *stride) {
    DataBuffer *db = fromBase(b);
    *data = db->data.data();
    *format = DRM_FORMAT_ARGB8888;
    *stride = db->w * 4;
    return true;
  }

  void DataBuffer::implEndDataPtr(wlr_buffer *) {}

  // get_dmabuf / get_shm are null: this buffer is only ever read back on the
  // CPU (pixman) via begin/end_data_ptr_access.
  const wlr_buffer_impl DataBuffer::impl = {
    .destroy = DataBuffer::implDestroy,
    .get_dmabuf = nullptr,
    .get_shm = nullptr,
    .begin_data_ptr_access = DataBuffer::implBeginDataPtr,
    .end_data_ptr_access = DataBuffer::implEndDataPtr,
  };

  DataBuffer *DataBuffer::create(uint32_t w, uint32_t h, std::vector<uint32_t> pixels) {
    DataBuffer *db = new DataBuffer();
    db->data = std::move(pixels);
    db->w = w;
    db->h = h;
    wlr_buffer_init(&db->buffer, &DataBuffer::impl,
                    static_cast<int>(w), static_cast<int>(h));
    return db;
  }

} // namespace bbai
