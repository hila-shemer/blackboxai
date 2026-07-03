// SNI item model - the pure half of the tray. No sd-bus types in here: raw
// bytes in, structs out, so every helper is L0-testable. The Host (SniHost.hh)
// fills these from the bus; the wave-2 slit renders them.
#ifndef BLACKBOXAI_SNI_HH
#define BLACKBOXAI_SNI_HH

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace bbai::sni {

  // One icon size as shipped over the bus: ARGB32 in NETWORK byte order
  // (A,R,G,B bytes per pixel, per the SNI spec). Bytes stay raw in the model -
  // a misbehaving app is then a rendering bug, not model corruption; convert
  // at the render seam.
  struct IconFrame {
    int width = 0, height = 0;
    std::vector<uint8_t> data;   // 4 * width * height bytes

    // Host-order packed pixels (0xAARRGGBB) - what the renderer wants.
    std::vector<uint32_t> toNative() const {
      std::vector<uint32_t> px;
      const std::size_t n = data.size() / 4;
      px.reserve(n);
      for (std::size_t i = 0; i < n; ++i)
        px.push_back((uint32_t(data[4 * i]) << 24) |
                     (uint32_t(data[4 * i + 1]) << 16) |
                     (uint32_t(data[4 * i + 2]) << 8) |
                      uint32_t(data[4 * i + 3]));
      return px;
    }
  };

  struct Item {
    std::string service;     // bus name (unique or well-known)
    std::string path;        // object path of the item
    std::string id, title;
    std::string status;      // Passive | Active | NeedsAttention
    std::string icon_name, icon_theme_path;
    std::vector<IconFrame> icon_pixmaps;
    std::string tooltip;     // ToolTip's title component
    // dbusmenu is wave-2 menus territory (program decision 2026-07-03): we
    // carry the path and the flag, nothing else.
    std::string menu_path;   // com.canonical.dbusmenu object path ("" = none)
    bool item_is_menu = false;
  };

  // RegisterStatusNotifierItem's argument is EITHER an object path
  // ("/StatusNotifierItem", ayatana/libappindicator style - the sender supplies
  // the service) OR a service name (KDE style - path defaults per spec). Both
  // conventions are alive in the wild.
  struct Registration { std::string service, path; };

  inline Registration resolveRegistration(const std::string &arg,
                                          const std::string &sender) {
    if (!arg.empty() && arg[0] == '/') return {sender, arg};
    return {arg, "/StatusNotifierItem"};
  }

  // The frame closest to `target` px, preferring >= target (downscaling beats
  // upscaling). nullptr when frames is empty or all frames are degenerate.
  inline const IconFrame *pickBestFrame(const std::vector<IconFrame> &frames,
                                        int target) {
    const IconFrame *best = nullptr;
    for (const IconFrame &f : frames) {
      if (f.width <= 0 || f.height <= 0) continue;
      if (!best) { best = &f; continue; }
      const int fs = std::max(f.width, f.height);
      const int bs = std::max(best->width, best->height);
      const bool f_ge = fs >= target, b_ge = bs >= target;
      if (f_ge != b_ge) { if (f_ge) best = &f; continue; }
      if (std::abs(fs - target) < std::abs(bs - target)) best = &f;
    }
    return best;
  }

} // namespace bbai::sni

#endif // BLACKBOXAI_SNI_HH
