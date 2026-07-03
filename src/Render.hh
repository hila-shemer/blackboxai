// Parent_Relative emit: the element shows its parent's pixels (in X the parent
// pixmap showed through; here we crop the parent's rendered buffer). Shared by
// Decoration (labels/buttons over the titlebar) and Toolbar (sections over the
// bar). Out-of-bounds geometry falls back to flat black - the same rung the
// classic sanity rules apply to containers.
#ifndef BLACKBOXAI_RENDER_HH
#define BLACKBOXAI_RENDER_HH

#include <cstdint>
#include <vector>

namespace bbai::render {

  inline std::vector<uint32_t> cropOrBlack(const std::vector<uint32_t> &parent,
                                           int parentW, int parentH,
                                           int x, int y, int w, int h) {
    std::vector<uint32_t> out(static_cast<size_t>(w) * h, 0xFF000000u);
    if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
        x + w > parentW || y + h > parentH ||
        parent.size() < static_cast<size_t>(parentW) * parentH)
      return out;   // flat-black rung
    for (int row = 0; row < h; ++row)
      for (int col = 0; col < w; ++col)
        out[static_cast<size_t>(row) * w + col] =
          parent[static_cast<size_t>(y + row) * parentW + (x + col)];
    return out;
  }

} // namespace bbai::render

#endif // BLACKBOXAI_RENDER_HH
