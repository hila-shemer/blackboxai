// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Ported to BlackboxAI (Wayland) from blackboxwm lib/Color.cc:
// the X color allocation is gone; Color::fromString() parses color specs that
// the X server's XParseColor used to resolve.

#include "Color.hh"

#include <cctype>
#include <unordered_map>

namespace {
  int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
  }

  // X11 rgb.txt gray0..gray100, copied verbatim. The upstream ramp is
  // hand-authored - gray30 is 77 (76.5 up) but gray50 is 127 (127.5 down) -
  // so a formula can't reproduce it; the table can.
  constexpr unsigned char kGrayRamp[101] = {
      0,   3,   5,   8,  10,  13,  15,  18,  20,  23,  26,  28,  31,  33,  36,
     38,  41,  43,  46,  48,  51,  54,  56,  59,  61,  64,  66,  69,  71,  74,
     77,  79,  82,  84,  87,  89,  92,  94,  97,  99, 102, 105, 107, 110, 112,
    115, 117, 120, 122, 125, 127, 130, 133, 135, 138, 140, 143, 145, 148, 150,
    153, 156, 158, 161, 163, 166, 168, 171, 173, 176, 179, 181, 184, 186, 189,
    191, 194, 196, 199, 201, 204, 207, 209, 212, 214, 217, 219, 222, 224, 227,
    229, 232, 235, 237, 240, 242, 245, 247, 250, 252, 255 };

  // "greyN"/"grayN" -> kGrayRamp[N]; input is already lowercased.
  bool grayRamp(const std::string &lower, bt::Color &out) {
    if (lower.size() < 5) return false;
    if (lower.compare(0, 4, "grey") != 0 && lower.compare(0, 4, "gray") != 0)
      return false;
    int n = 0;
    for (size_t i = 4; i < lower.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(lower[i]))) return false;
      n = n * 10 + (lower[i] - '0');
      if (n > 100) return false;
    }
    out = bt::Color(kGrayRamp[n], kGrayRamp[n], kGrayRamp[n]);
    return true;
  }

  // Named colors used by the 19 shipped styles + their rootCommands. The greyN
  // family goes through grayRamp(); this table is only the non-ramp names.
  const std::unordered_map<std::string, bt::Color> &namedColors() {
    static const std::unordered_map<std::string, bt::Color> t = {
      {"black", bt::Color(0,0,0)},        {"white", bt::Color(255,255,255)},
      {"red", bt::Color(255,0,0)},        {"green", bt::Color(0,255,0)},
      {"blue", bt::Color(0,0,255)},
      {"grey", bt::Color(190,190,190)},   {"gray", bt::Color(190,190,190)},
      {"darkgrey", bt::Color(169,169,169)}, {"darkgray", bt::Color(169,169,169)},
      {"midnightblue", bt::Color(25,25,112)},
      {"steelblue", bt::Color(70,130,180)},
      {"slategrey", bt::Color(112,128,144)}, {"slategray", bt::Color(112,128,144)},
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
      int v[3] = {-1,-1,-1};
      size_t i = 4, k = 0;
      while (k < 3 && i < spec.size()) {
        int hi = hexNibble(spec[i]);
        if (hi < 0) return Color();
        int lo = (i+1 < spec.size() && hexNibble(spec[i+1]) >= 0)
                   ? hexNibble(spec[++i]) : hi;
        v[k++] = hi * 16 + lo;
        ++i;
        if (k < 3) { if (i >= spec.size() || spec[i] != '/') return Color(); ++i; }
      }
      if (k != 3) return Color();
      return Color(v[0], v[1], v[2]);
    }

    std::string lower;
    for (char c : spec) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto it = namedColors().find(lower);
    if (it != namedColors().end()) return it->second;
    Color ramp;
    if (grayRamp(lower, ramp)) return ramp;
    return Color();
  }

} // namespace bt
