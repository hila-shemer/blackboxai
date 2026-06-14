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

  // Minimal named-color table covering the colors used by the bundled styles.
  // Extend toward full rgb.txt parsing in a later refinement.
  const std::unordered_map<std::string, bt::Color> &namedColors() {
    static const std::unordered_map<std::string, bt::Color> t = {
      {"black", bt::Color(0,0,0)}, {"white", bt::Color(255,255,255)},
      {"red", bt::Color(255,0,0)}, {"green", bt::Color(0,255,0)},
      {"blue", bt::Color(0,0,255)},
      {"grey", bt::Color(190,190,190)}, {"gray", bt::Color(190,190,190)},
      {"grey20", bt::Color(51,51,51)}, {"grey40", bt::Color(102,102,102)},
      {"grey60", bt::Color(153,153,153)}, {"grey80", bt::Color(204,204,204)},
      {"darkgrey", bt::Color(169,169,169)},
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
    return it == namedColors().end() ? Color() : it->second;
  }

} // namespace bt
