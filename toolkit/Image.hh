// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Image.hh for Blackbox - an X11 Window manager
// Copyright (c) 2001 - 2005 Sean 'Shaleh' Perry <shaleh@debian.org>
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
// Ported to BlackboxAI (Wayland) from blackboxwm lib/Image.hh:
// the CPU gradient/bevel renderer is kept verbatim (it fills a packed RGB
// data[] buffer); the X pixmap tail (XColorTable, SHM, Floyd-Steinberg/ordered
// dither, renderPixmap) is deleted and replaced by renderBuffer(), which packs
// data[] into an ARGB8888 buffer for upload as a wlr_scene_buffer.

#ifndef BLACKBOXAI_IMAGE_HH
#define BLACKBOXAI_IMAGE_HH

#include <vector>
#include <cstdint>
#include "Color.hh"

namespace bt {

  class Texture;

  // 3-byte RGB cell (the upstream 8:8:8:8 'reserved' bitfield is dropped; it is
  // never referenced by the gradient/bevel math).
  struct RGB { unsigned char red, green, blue; };

  class Image {
  public:
    Image(unsigned int w, unsigned int h);
    ~Image();
    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;

    // Render a fully-composed appearance for `texture` into a packed ARGB8888
    // buffer (row-major, width*height, alpha = 0xFF). Replaces the old
    // render()->renderPixmap()->drawTexture() X path with a single buffer.
    std::vector<uint32_t> renderBuffer(const Texture &texture);

  private:
    RGB *data;
    unsigned int width, height;

    // verbatim from upstream (these fill `data`):
    void raisedBevel(unsigned int border_width);
    void sunkenBevel(unsigned int border_width);
    void dgradient(const Color &from, const Color &to, bool interlaced);
    void egradient(const Color &from, const Color &to, bool interlaced);
    void hgradient(const Color &from, const Color &to, bool interlaced);
    void pgradient(const Color &from, const Color &to, bool interlaced);
    void rgradient(const Color &from, const Color &to, bool interlaced);
    void partial_vgradient(const Color &from, const Color &to, bool interlaced,
                           unsigned int fromHeight, unsigned int toHeight);
    void cdgradient(const Color &from, const Color &to, bool interlaced);
    void pcgradient(const Color &from, const Color &to, bool interlaced);
    void svgradient(const Color &from, const Color &to, bool interlaced);

    // new (Wayland) helpers:
    void fillSolid(const Color &c);
    void drawBorder(const Color &c, unsigned int bw);
    void applyInterlace();
  };

} // namespace bt

#endif // BLACKBOXAI_IMAGE_HH
