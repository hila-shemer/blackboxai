#include <doctest/doctest.h>
#include "DataBuffer.hh"
#include <drm_fourcc.h>
#include <vector>
#include <cstdint>

TEST_CASE("DataBuffer exposes its ARGB8888 pixels via data-ptr access") {
    std::vector<uint32_t> px(2 * 2, 0xFF112233u);
    bbai::DataBuffer *db = bbai::DataBuffer::create(2, 2, px);

    void *data = nullptr; uint32_t fmt = 0; size_t stride = 0;
    REQUIRE(wlr_buffer_begin_data_ptr_access(db->base(),
        WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &fmt, &stride));
    CHECK(fmt == DRM_FORMAT_ARGB8888);
    CHECK(stride == 2u * 4u);
    CHECK(reinterpret_cast<uint32_t *>(data)[0] == 0xFF112233u);
    CHECK(reinterpret_cast<uint32_t *>(data)[3] == 0xFF112233u);
    wlr_buffer_end_data_ptr_access(db->base());

    wlr_buffer_drop(db->base());  // refcount 0 -> self-destructs
}
