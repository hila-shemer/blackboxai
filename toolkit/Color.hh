// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Color.hh for Blackbox - an X11 Window manager
// Copyright (c) 2001 - 2005 Sean 'Shaleh' Perry <shaleh at debian.org>
// Copyright (c) 1997 - 2000, 2002 - 2005
//         Bradley T Hughes <bhughes at trolltech.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
// Ported to BlackboxAI (Wayland) from blackboxwm lib/Color.hh:
// screen-independent RGB kept; X pixel allocation (pixel()/deallocate()/
// PenCache friend/_screen/_pixel) removed; namedColor() (X server query)
// replaced by the standalone parser Color::fromString().

#ifndef BLACKBOXAI_COLOR_HH
#define BLACKBOXAI_COLOR_HH

#include <string>

namespace bt {

  /*
    The color object.  Colors are stored in rgb format (screen
    independent).  On a truecolor Wayland compositor there is no palette,
    so there are no screen-dependent pixel values to allocate.
  */
  class Color {
  public:
    explicit Color(int r = -1, int g = -1, int b = -1)
      : _red(r), _green(g), _blue(b) {}

    // Parse "#rgb", "#rrggbb", "rgb:rr/gg/bb", or a named color.
    // Returns an invalid Color (valid()==false) on failure.
    static Color fromString(const std::string &spec);

    int   red() const { return _red; }
    int green() const { return _green; }
    int  blue() const { return _blue; }
    void setRGB(int r, int g, int b) { _red = r; _green = g; _blue = b; }

    bool valid() const { return _red >= 0 && _green >= 0 && _blue >= 0; }

    bool operator==(const Color &c) const
    { return _red == c._red && _green == c._green && _blue == c._blue; }
    bool operator!=(const Color &c) const { return !operator==(c); }

  private:
    int _red, _green, _blue;
  };

} // namespace bt

#endif // BLACKBOXAI_COLOR_HH
