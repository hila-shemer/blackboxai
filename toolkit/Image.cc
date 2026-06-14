// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Image.cc for Blackbox - an X11 Window manager
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
// Ported to BlackboxAI (Wayland) from blackboxwm lib/Image.cc:
// the CPU gradient/bevel renderer (raisedBevel, sunkenBevel, dgradient,
// hgradient, partial_vgradient, pgradient, rgradient, egradient, pcgradient,
// cdgradient, svgradient) is kept verbatim from upstream; the X pixmap tail
// (XColorTable, SHM, Floyd-Steinberg/ordered dither, render/renderPixmap) is
// deleted and replaced by the constructor/helpers/renderBuffer() below, which
// pack data[] into an ARGB8888 buffer for upload as a wlr_scene_buffer.

#include "Image.hh"
#include "Texture.hh"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace bt {
  Image::Image(unsigned int w, unsigned int h)
    : data(new RGB[w * h]), width(w), height(h) {}
  Image::~Image() { delete [] data; }

  void Image::fillSolid(const Color &c) {
    RGB v{ (unsigned char)c.red(), (unsigned char)c.green(), (unsigned char)c.blue() };
    for (unsigned int i = 0; i < width * height; ++i) data[i] = v;
  }

  void Image::drawBorder(const Color &c, unsigned int bw) {
    RGB v{ (unsigned char)c.red(), (unsigned char)c.green(), (unsigned char)c.blue() };
    for (unsigned int i = 0; i < bw && i * 2 < width && i * 2 < height; ++i) {
      for (unsigned int x = i; x < width - i; ++x) {
        data[i * width + x] = v;
        data[(height - 1 - i) * width + x] = v;
      }
      for (unsigned int y = i; y < height - i; ++y) {
        data[y * width + i] = v;
        data[y * width + (width - 1 - i)] = v;
      }
    }
  }

  void Image::applyInterlace() {
    for (unsigned int y = 1; y < height; y += 2)
      for (unsigned int x = 0; x < width; ++x) {
        RGB &p = data[y * width + x];
        p.red   = (unsigned char)(p.red   * 7 / 8);
        p.green = (unsigned char)(p.green * 7 / 8);
        p.blue  = (unsigned char)(p.blue  * 7 / 8);
      }
  }

  std::vector<uint32_t> Image::renderBuffer(const Texture &texture) {
    const Color from = texture.color1(), to = texture.color2();
    const bool interlaced = texture.texture() & Texture::Interlaced;

    if (texture.texture() & Texture::Gradient) {
      if      (texture.texture() & Texture::Diagonal)      dgradient(from, to, interlaced);
      else if (texture.texture() & Texture::Elliptic)      egradient(from, to, interlaced);
      else if (texture.texture() & Texture::Horizontal)    hgradient(from, to, interlaced);
      else if (texture.texture() & Texture::Pyramid)       pgradient(from, to, interlaced);
      else if (texture.texture() & Texture::Rectangle)     rgradient(from, to, interlaced);
      else if (texture.texture() & Texture::Vertical)      partial_vgradient(from, to, interlaced, 0, height);
      else if (texture.texture() & Texture::CrossDiagonal) cdgradient(from, to, interlaced);
      else if (texture.texture() & Texture::PipeCross)     pcgradient(from, to, interlaced);
      else if (texture.texture() & Texture::SplitVertical) svgradient(from, to, interlaced);
      else fillSolid(from);
    } else {
      fillSolid(from);
      if (interlaced) applyInterlace();
    }

    if      (texture.texture() & Texture::Raised) raisedBevel(texture.borderWidth());
    else if (texture.texture() & Texture::Sunken) sunkenBevel(texture.borderWidth());

    if (texture.texture() & Texture::Border)
      drawBorder(texture.borderColor(), texture.borderWidth() ? texture.borderWidth() : 1);

    std::vector<uint32_t> out(width * height);
    for (unsigned int i = 0; i < width * height; ++i)
      out[i] = (0xFFu << 24) | (uint32_t(data[i].red) << 16)
             | (uint32_t(data[i].green) << 8) | data[i].blue;
    return out;
  }


// ---------------------------------------------------------------------------
// Below: the verbatim CPU gradient/bevel renderer from upstream
// blackboxwm lib/Image.cc (lines 1012..1970). Only the excess 4th aggregate
// initializer for the dropped RGB `reserved` field is removed in
// partial_vgradient (our RGB has only red/green/blue); no arithmetic changed.
// ---------------------------------------------------------------------------

void bt::Image::raisedBevel(unsigned int border_width) {
  if (width <= 2 || height <= 2 ||
      width <= (border_width * 4) || height <= (border_width * 4))
    return;

  RGB *p = data + (border_width * width) + border_width;
  unsigned int w = width - (border_width * 2);
  unsigned int h = height - (border_width * 2) - 2;
  unsigned char rr, gg, bb;

  // top of the bevel
  do {
    rr = p->red   + (p->red   >> 1);
    gg = p->green + (p->green >> 1);
    bb = p->blue  + (p->blue  >> 1);

    if (rr < p->red  )
      rr = ~0;
    if (gg < p->green)
      gg = ~0;
    if (bb < p->blue )
      bb = ~0;

    p->red = rr;
    p->green = gg;
    p->blue = bb;

    ++p;
  } while (--w);

  p += border_width + border_width;
  w = width - (border_width * 2);

  // left and right of the bevel
  do {
    rr = p->red   + (p->red   >> 1);
    gg = p->green + (p->green >> 1);
    bb = p->blue  + (p->blue  >> 1);

    if (rr < p->red)
      rr = ~0;
    if (gg < p->green)
      gg = ~0;
    if (bb < p->blue)
      bb = ~0;

    p->red = rr;
    p->green = gg;
    p->blue = bb;

    p += w - 1;

    rr = (p->red   >> 2) + (p->red   >> 1);
    gg = (p->green >> 2) + (p->green >> 1);
    bb = (p->blue  >> 2) + (p->blue  >> 1);

    if (rr > p->red  )
      rr = 0;
    if (gg > p->green)
      gg = 0;
    if (bb > p->blue )
      bb = 0;

    p->red   = rr;
    p->green = gg;
    p->blue  = bb;

    p += border_width + border_width + 1;
  } while (--h);

  w = width - (border_width * 2);

  // bottom of the bevel
  do {
    rr = (p->red   >> 2) + (p->red   >> 1);
    gg = (p->green >> 2) + (p->green >> 1);
    bb = (p->blue  >> 2) + (p->blue  >> 1);

    if (rr > p->red  )
      rr = 0;
    if (gg > p->green)
      gg = 0;
    if (bb > p->blue )
      bb = 0;

    p->red   = rr;
    p->green = gg;
    p->blue  = bb;

    ++p;
  } while (--w);
}


void bt::Image::sunkenBevel(unsigned int border_width) {
  if (width <= 2 || height <= 2 ||
      width <= (border_width * 4) || height <= (border_width * 4))
    return;

  RGB *p = data + (border_width * width) + border_width;
  unsigned int w = width - (border_width * 2);
  unsigned int h = height - (border_width * 2) - 2;
  unsigned char rr, gg, bb;

  // top of the bevel
  do {
    rr = (p->red   >> 2) + (p->red   >> 1);
    gg = (p->green >> 2) + (p->green >> 1);
    bb = (p->blue  >> 2) + (p->blue  >> 1);

    if (rr > p->red  )
      rr = 0;
    if (gg > p->green)
      gg = 0;
    if (bb > p->blue )
      bb = 0;

    p->red = rr;
    p->green = gg;
    p->blue = bb;

    ++p;
  } while (--w);

  p += border_width + border_width;
  w = width - (border_width * 2);

  // left and right of the bevel
  do {
    rr = (p->red   >> 2) + (p->red   >> 1);
    gg = (p->green >> 2) + (p->green >> 1);
    bb = (p->blue  >> 2) + (p->blue  >> 1);

    if (rr > p->red  )
      rr = 0;
    if (gg > p->green)
      gg = 0;
    if (bb > p->blue )
      bb = 0;

    p->red = rr;
    p->green = gg;
    p->blue = bb;

    p += w - 1;

    rr = p->red   + (p->red   >> 1);
    gg = p->green + (p->green >> 1);
    bb = p->blue  + (p->blue  >> 1);

    if (rr < p->red)
      rr = ~0;
    if (gg < p->green)
      gg = ~0;
    if (bb < p->blue)
      bb = ~0;

    p->red   = rr;
    p->green = gg;
    p->blue  = bb;

    p += border_width + border_width + 1;
  } while (--h);

  w = width - (border_width * 2);

  // bottom of the bevel
  do {
    rr = p->red   + (p->red   >> 1);
    gg = p->green + (p->green >> 1);
    bb = p->blue  + (p->blue  >> 1);

    if (rr < p->red)
      rr = ~0;
    if (gg < p->green)
      gg = ~0;
    if (bb < p->blue)
      bb = ~0;

    p->red   = rr;
    p->green = gg;
    p->blue  = bb;

    ++p;
  } while (--w);
}


void bt::Image::dgradient(const Color &from, const Color &to,
                          bool interlaced) {
  // diagonal gradient code was written by Mike Cole <mike@mydot.com>
  // modified for interlacing by Brad Hughes

  double drx, dgx, dbx, dry, dgy, dby;
  double yr = 0.0, yg = 0.0, yb = 0.0,
         xr = static_cast<double>(from.red()),
         xg = static_cast<double>(from.green()),
         xb = static_cast<double>(from.blue());

  RGB *p = data;
  unsigned int w = width * 2, h = height * 2;
  unsigned int x, y;

  const unsigned int dimension = std::max(width, height);
  unsigned int *alloc = new unsigned int[dimension * 6];
  unsigned int *xt[3], *yt[3];
  xt[0] = alloc + (dimension * 0);
  xt[1] = alloc + (dimension * 1);
  xt[2] = alloc + (dimension * 2);
  yt[0] = alloc + (dimension * 3);
  yt[1] = alloc + (dimension * 4);
  yt[2] = alloc + (dimension * 5);

  dry = drx = static_cast<double>(to.red()   - from.red());
  dgy = dgx = static_cast<double>(to.green() - from.green());
  dby = dbx = static_cast<double>(to.blue()  - from.blue());

  // Create X table
  drx /= w;
  dgx /= w;
  dbx /= w;

  for (x = 0; x < width; ++x) {
    xt[0][x] = static_cast<unsigned char>(xr);
    xt[1][x] = static_cast<unsigned char>(xg);
    xt[2][x] = static_cast<unsigned char>(xb);

    xr += drx;
    xg += dgx;
    xb += dbx;
  }

  // Create Y table
  dry /= h;
  dgy /= h;
  dby /= h;

  for (y = 0; y < height; ++y) {
    yt[0][y] = static_cast<unsigned char>(yr);
    yt[1][y] = static_cast<unsigned char>(yg);
    yt[2][y] = static_cast<unsigned char>(yb);

    yr += dry;
    yg += dgy;
    yb += dby;
  }

  // Combine tables to create gradient

  if (!interlaced) {
    // normal dgradient
    for (y = 0; y < height; ++y) {
      for (x = 0; x < width; ++x, ++p) {
        p->red   = xt[0][x] + yt[0][y];
        p->green = xt[1][x] + yt[1][y];
        p->blue  = xt[2][x] + yt[2][y];
      }
    }
  } else {
    // interlacing effect
    for (y = 0; y < height; ++y) {
      for (x = 0; x < width; ++x, ++p) {
        p->red   = xt[0][x] + yt[0][y];
        p->green = xt[1][x] + yt[1][y];
        p->blue  = xt[2][x] + yt[2][y];

        if (y & 1) {
          p->red   = (p->red   >> 1) + (p->red   >> 2);
          p->green = (p->green >> 1) + (p->green >> 2);
          p->blue  = (p->blue  >> 1) + (p->blue  >> 2);
        }
      }
    }
  }

  delete [] alloc;
}


void bt::Image::hgradient(const Color &from, const Color &to,
                          bool interlaced) {
  double drx, dgx, dbx,
    xr = static_cast<double>(from.red()),
    xg = static_cast<double>(from.green()),
    xb = static_cast<double>(from.blue());
  RGB *p = data;
  unsigned int total = width * (height - 2);
  unsigned int x;

  drx = static_cast<double>(to.red()   - from.red());
  dgx = static_cast<double>(to.green() - from.green());
  dbx = static_cast<double>(to.blue()  - from.blue());

  drx /= width;
  dgx /= width;
  dbx /= width;

  if (interlaced && height > 1) {
    // interlacing effect

    // first line
    for (x = 0; x < width; ++x, ++p) {
      p->red   = static_cast<unsigned char>(xr);
      p->green = static_cast<unsigned char>(xg);
      p->blue  = static_cast<unsigned char>(xb);

      xr += drx;
      xg += dgx;
      xb += dbx;
    }

    // second line
    xr = static_cast<double>(from.red()),
    xg = static_cast<double>(from.green()),
    xb = static_cast<double>(from.blue());

    for (x = 0; x < width; ++x, ++p) {
      p->red   = static_cast<unsigned char>(xr);
      p->green = static_cast<unsigned char>(xg);
      p->blue  = static_cast<unsigned char>(xb);

      p->red   = (p->red   >> 1) + (p->red   >> 2);
      p->green = (p->green >> 1) + (p->green >> 2);
      p->blue  = (p->blue  >> 1) + (p->blue  >> 2);

      xr += drx;
      xg += dgx;
      xb += dbx;
    }
  } else {
    // first line
    for (x = 0; x < width; ++x, ++p) {
      p->red   = static_cast<unsigned char>(xr);
      p->green = static_cast<unsigned char>(xg);
      p->blue  = static_cast<unsigned char>(xb);

      xr += drx;
      xg += dgx;
      xb += dbx;
    }

    if (height > 1) {
      // second line
      memcpy(p, data, width * sizeof(RGB));
      p += width;
    }
  }

  if (height > 2) {
    // rest of the gradient
    for (x = 0; x < total; ++x)
      p[x] = data[x];
  }
}


void bt::Image::partial_vgradient(const Color &from, const Color &to,
                                  bool interlaced,
                                  unsigned int fromHeight,
                                  unsigned int toHeight)
{
  double yr = static_cast<double>(from.red()  );
  double yg = static_cast<double>(from.green());
  double yb = static_cast<double>(from.blue() );

  double dry = static_cast<double>(to.red()   - from.red()  );
  double dgy = static_cast<double>(to.green() - from.green());
  double dby = static_cast<double>(to.blue()  - from.blue() );

  unsigned int deltaHeight = toHeight - fromHeight;
  dry /= deltaHeight;
  dgy /= deltaHeight;
  dby /= deltaHeight;

  RGB *p = data + width*fromHeight;
  unsigned int x, y;


  if (interlaced) {
    // faked interlacing effect
    for (y = fromHeight; y < toHeight; ++y) {
      const RGB rgb = {
        static_cast<unsigned char>((y & 1) ? (yr * 3. / 4.) : yr),
        static_cast<unsigned char>((y & 1) ? (yg * 3. / 4.) : yg),
        static_cast<unsigned char>((y & 1) ? (yb * 3. / 4.) : yb)
      };
      for (x = 0; x < width; ++x, ++p)
        *p = rgb;

      yr += dry;
      yg += dgy;
      yb += dby;
    }
  } else {
    // normal vgradient
      for (y = fromHeight; y < toHeight; ++y) {
        const RGB rgb = {
        static_cast<unsigned char>(yr),
        static_cast<unsigned char>(yg),
        static_cast<unsigned char>(yb)
      };
      for (x = 0; x < width; ++x, ++p)
        *p = rgb;

      yr += dry;
      yg += dgy;
      yb += dby;
    }
  }
}


void bt::Image::pgradient(const Color &from, const Color &to,
                          bool interlaced) {
  // pyramid gradient -  based on original dgradient, written by
  // Mosfet (mosfet@kde.org)
  // adapted from kde sources for Blackbox by Brad Hughes

  double yr, yg, yb, drx, dgx, dbx, dry, dgy, dby, xr, xg, xb;
  int rsign, gsign, bsign;
  RGB *p = data;
  unsigned int tr = to.red(), tg = to.green(), tb = to.blue();
  unsigned int x, y;

  const unsigned int dimension = std::max(width, height);
  unsigned int *alloc = new unsigned int[dimension * 6];
  unsigned int *xt[3], *yt[3];
  xt[0] = alloc + (dimension * 0);
  xt[1] = alloc + (dimension * 1);
  xt[2] = alloc + (dimension * 2);
  yt[0] = alloc + (dimension * 3);
  yt[1] = alloc + (dimension * 4);
  yt[2] = alloc + (dimension * 5);

  dry = drx = static_cast<double>(to.red()   - from.red());
  dgy = dgx = static_cast<double>(to.green() - from.green());
  dby = dbx = static_cast<double>(to.blue()  - from.blue());

  rsign = (drx < 0) ? -1 : 1;
  gsign = (dgx < 0) ? -1 : 1;
  bsign = (dbx < 0) ? -1 : 1;

  xr = yr = (drx / 2);
  xg = yg = (dgx / 2);
  xb = yb = (dbx / 2);

  // Create X table
  drx /= width;
  dgx /= width;
  dbx /= width;

  for (x = 0; x < width; ++x) {
    xt[0][x] = static_cast<unsigned char>(fabs(xr));
    xt[1][x] = static_cast<unsigned char>(fabs(xg));
    xt[2][x] = static_cast<unsigned char>(fabs(xb));

    xr -= drx;
    xg -= dgx;
    xb -= dbx;
  }

  // Create Y table
  dry /= height;
  dgy /= height;
  dby /= height;

  for (y = 0; y < height; ++y) {
    yt[0][y] = static_cast<unsigned char>(fabs(yr));
    yt[1][y] = static_cast<unsigned char>(fabs(yg));
    yt[2][y] = static_cast<unsigned char>(fabs(yb));

    yr -= dry;
    yg -= dgy;
    yb -= dby;
  }

  // Combine tables to create gradient

  if (!interlaced) {
    // normal pgradient
    for (y = 0; y < height; ++y) {
      for (x = 0; x < width; ++x, ++p) {
        p->red =
          static_cast<unsigned char>(tr - (rsign * (xt[0][x] + yt[0][y])));
        p->green =
          static_cast<unsigned char>(tg - (gsign * (xt[1][x] + yt[1][y])));
        p->blue =
          static_cast<unsigned char>(tb - (bsign * (xt[2][x] + yt[2][y])));
      }
    }
  } else {
    // interlacing effect
    for (y = 0; y < height; ++y) {
      for (x = 0; x < width; ++x, ++p) {
        p->red =
          static_cast<unsigned char>(tr - (rsign * (xt[0][x] + yt[0][y])));
        p->green =
          static_cast<unsigned char>(tg - (gsign * (xt[1][x] + yt[1][y])));
        p->blue =
          static_cast<unsigned char>(tb - (bsign * (xt[2][x] + yt[2][y])));

        if (y & 1) {
          p->red   = (p->red   >> 1) + (p->red   >> 2);
          p->green = (p->green >> 1) + (p->green >> 2);
          p->blue  = (p->blue  >> 1) + (p->blue  >> 2);
        }
      }
    }
  }

  delete [] alloc;
}


void bt::Image::rgradient(const Color &from, const Color &to,
                          bool interlaced) {
  // rectangle gradient -  based on original dgradient, written by
  // Mosfet (mosfet@kde.org)
  // adapted from kde sources for Blackbox by Brad Hughes

  double drx, dgx, dbx, dry, dgy, dby, xr, xg, xb, yr, yg, yb;
  int rsign, gsign, bsign;
  RGB *p = data;
  unsigned int tr = to.red(), tg = to.green(), tb = to.blue();
  unsigned int x, y;

  const unsigned int dimension = std::max(width, height);
  unsigned int *alloc = new unsigned int[dimension * 6];
  unsigned int *xt[3], *yt[3];
  xt[0] = alloc + (dimension * 0);
  xt[1] = alloc + (dimension * 1);
  xt[2] = alloc + (dimension * 2);
  yt[0] = alloc + (dimension * 3);
  yt[1] = alloc + (dimension * 4);
  yt[2] = alloc + (dimension * 5);

  dry = drx = static_cast<double>(to.red()   - from.red());
  dgy = dgx = static_cast<double>(to.green() - from.green());
  dby = dbx = static_cast<double>(to.blue()  - from.blue());

  rsign = (drx < 0) ? -2 : 2;
  gsign = (dgx < 0) ? -2 : 2;
  bsign = (dbx < 0) ? -2 : 2;

  xr = yr = (drx / 2);
  xg = yg = (dgx / 2);
  xb = yb = (dbx / 2);

  // Create X table
  drx /= width;
  dgx /= width;
  dbx /= width;

  for (x = 0; x < width; ++x) {
    xt[0][x] = static_cast<unsigned char>(fabs(xr));
    xt[1][x] = static_cast<unsigned char>(fabs(xg));
    xt[2][x] = static_cast<unsigned char>(fabs(xb));

    xr -= drx;
    xg -= dgx;
    xb -= dbx;
  }

  // Create Y table
  dry /= height;
  dgy /= height;
  dby /= height;

  for (y = 0; y < height; ++y) {
    yt[0][y] = static_cast<unsigned char>(fabs(yr));
    yt[1][y] = static_cast<unsigned char>(fabs(yg));
    yt[2][y] = static_cast<unsigned char>(fabs(yb));

    yr -= dry;
    yg -= dgy;
    yb -= dby;
  }

  // Combine tables to create gradient

  if (!interlaced) {
    // normal rgradient
    for (y = 0; y < height; ++y) {
      for (x = 0; x < width; ++x, ++p) {
        p->red =
          static_cast<unsigned char>(tr - (rsign *
                                           std::max(xt[0][x], yt[0][y])));
        p->green =
          static_cast<unsigned char>(tg - (gsign *
                                           std::max(xt[1][x], yt[1][y])));
        p->blue =
          static_cast<unsigned char>(tb - (bsign *
                                           std::max(xt[2][x], yt[2][y])));
      }
    }
  } else {
    // interlacing effect
    for (y = 0; y < height; ++y) {
      for (x = 0; x < width; ++x, ++p) {
        p->red =
          static_cast<unsigned char>(tr - (rsign *
                                           std::max(xt[0][x], yt[0][y])));
        p->green =
          static_cast<unsigned char>(tg - (gsign *
                                           std::max(xt[1][x], yt[1][y])));
        p->blue =
          static_cast<unsigned char>(tb - (bsign *
                                           std::max(xt[2][x], yt[2][y])));

        if (y & 1) {
          p->red   = (p->red   >> 1) + (p->red   >> 2);
          p->green = (p->green >> 1) + (p->green >> 2);
          p->blue  = (p->blue  >> 1) + (p->blue  >> 2);
        }
      }
    }
  }

  delete [] alloc;
}


void bt::Image::egradient(const Color &from, const Color &to,
                          bool interlaced) {
  // elliptic gradient -  based on original dgradient, written by
  // Mosfet (mosfet@kde.org)
  // adapted from kde sources for Blackbox by Brad Hughes

  double drx, dgx, dbx, dry, dgy, dby, yr, yg, yb, xr, xg, xb;
  int rsign, gsign, bsign;
  RGB *p = data;
  unsigned int tr = to.red(), tg = to.green(), tb = to.blue();
  unsigned int x, y;

  const unsigned int dimension = std::max(width, height);
  unsigned int *alloc = new unsigned int[dimension * 6];
  unsigned int *xt[3], *yt[3];
  xt[0] = alloc + (dimension * 0);
  xt[1] = alloc + (dimension * 1);
  xt[2] = alloc + (dimension * 2);
  yt[0] = alloc + (dimension * 3);
  yt[1] = alloc + (dimension * 4);
  yt[2] = alloc + (dimension * 5);

  dry = drx = static_cast<double>(to.red() - from.red());
  dgy = dgx = static_cast<double>(to.green() - from.green());
  dby = dbx = static_cast<double>(to.blue() - from.blue());

  rsign = (drx < 0) ? -1 : 1;
  gsign = (dgx < 0) ? -1 : 1;
  bsign = (dbx < 0) ? -1 : 1;

  xr = yr = (drx / 2);
  xg = yg = (dgx / 2);
  xb = yb = (dbx / 2);

  // Create X table
  drx /= width;
  dgx /= width;
  dbx /= width;

  for (x = 0; x < width; x++) {
    xt[0][x] = static_cast<unsigned int>(xr * xr);
    xt[1][x] = static_cast<unsigned int>(xg * xg);
    xt[2][x] = static_cast<unsigned int>(xb * xb);

    xr -= drx;
    xg -= dgx;
    xb -= dbx;
  }

  // Create Y table
  dry /= height;
  dgy /= height;
  dby /= height;

  for (y = 0; y < height; y++) {
    yt[0][y] = static_cast<unsigned int>(yr * yr);
    yt[1][y] = static_cast<unsigned int>(yg * yg);
    yt[2][y] = static_cast<unsigned int>(yb * yb);

    yr -= dry;
    yg -= dgy;
    yb -= dby;
  }

  // Combine tables to create gradient

  if (!interlaced) {
    // normal egradient
    for (y = 0; y < height; ++y) {
      for (x = 0; x < width; ++x, ++p) {
        p->red   = static_cast<unsigned char>
                   (tr - (rsign * static_cast<int>(sqrt(xt[0][x] +
                                                        yt[0][y]))));
        p->green = static_cast<unsigned char>
                   (tg - (gsign * static_cast<int>(sqrt(xt[1][x] +
                                                        yt[1][y]))));
        p->blue  = static_cast<unsigned char>
                   (tb - (bsign * static_cast<int>(sqrt(xt[2][x] +
                                                        yt[2][y]))));
      }
    }
  } else {
    // interlacing effect
    for (y = 0; y < height; ++y) {
      for (x = 0; x < width; ++x, ++p) {
        p->red   = static_cast<unsigned char>
                   (tr - (rsign * static_cast<int>(sqrt(xt[0][x]
                                                        + yt[0][y]))));
        p->green = static_cast<unsigned char>
                   (tg - (gsign * static_cast<int>(sqrt(xt[1][x]
                                                        + yt[1][y]))));
        p->blue  = static_cast<unsigned char>
                   (tb - (bsign * static_cast<int>(sqrt(xt[2][x]
                                                        + yt[2][y]))));

        if (y & 1) {
          p->red   = (p->red   >> 1) + (p->red   >> 2);
          p->green = (p->green >> 1) + (p->green >> 2);
          p->blue  = (p->blue  >> 1) + (p->blue  >> 2);
        }
      }
    }
  }

  delete [] alloc;
}


void bt::Image::pcgradient(const Color &from, const Color &to,
                           bool interlaced) {
  // pipe cross gradient -  based on original dgradient, written by
  // Mosfet (mosfet@kde.org)
  // adapted from kde sources for Blackbox by Brad Hughes

  double drx, dgx, dbx, dry, dgy, dby, xr, xg, xb, yr, yg, yb;
  int rsign, gsign, bsign;
  RGB *p = data;
  unsigned int tr = to.red(), tg = to.green(), tb = to.blue();
  unsigned int x, y;

  const unsigned int dimension = std::max(width, height);
  unsigned int *alloc = new unsigned int[dimension * 6];
  unsigned int *xt[3], *yt[3];
  xt[0] = alloc + (dimension * 0);
  xt[1] = alloc + (dimension * 1);
  xt[2] = alloc + (dimension * 2);
  yt[0] = alloc + (dimension * 3);
  yt[1] = alloc + (dimension * 4);
  yt[2] = alloc + (dimension * 5);

  dry = drx = static_cast<double>(to.red()   - from.red());
  dgy = dgx = static_cast<double>(to.green() - from.green());
  dby = dbx = static_cast<double>(to.blue()  - from.blue());

  rsign = (drx < 0) ? -2 : 2;
  gsign = (dgx < 0) ? -2 : 2;
  bsign = (dbx < 0) ? -2 : 2;

  xr = yr = (drx / 2);
  xg = yg = (dgx / 2);
  xb = yb = (dbx / 2);

  // Create X table
  drx /= width;
  dgx /= width;
  dbx /= width;

  for (x = 0; x < width; ++x) {
    xt[0][x] = static_cast<unsigned char>(fabs(xr));
    xt[1][x] = static_cast<unsigned char>(fabs(xg));
    xt[2][x] = static_cast<unsigned char>(fabs(xb));

    xr -= drx;
    xg -= dgx;
    xb -= dbx;
  }

  // Create Y table
  dry /= height;
  dgy /= height;
  dby /= height;

  for (y = 0; y < height; ++y) {
    yt[0][y] = static_cast<unsigned char>(fabs(yr));
    yt[1][y] = static_cast<unsigned char>(fabs(yg));
    yt[2][y] = static_cast<unsigned char>(fabs(yb));

    yr -= dry;
    yg -= dgy;
    yb -= dby;
  }

  // Combine tables to create gradient

  if (!interlaced) {
    // normal rgradient
    for (y = 0; y < height; ++y) {
      for (x = 0; x < width; ++x, ++p) {
        p->red =
          static_cast<unsigned char>(tr - (rsign *
                                           std::min(xt[0][x], yt[0][y])));
        p->green =
          static_cast<unsigned char>(tg - (gsign *
                                           std::min(xt[1][x], yt[1][y])));
        p->blue =
          static_cast<unsigned char>(tb - (bsign *
                                           std::min(xt[2][x], yt[2][y])));
      }
    }
  } else {
    // interlacing effect
    for (y = 0; y < height; ++y) {
      for (x = 0; x < width; ++x, ++p) {
        p->red =
          static_cast<unsigned char>(tr - (rsign *
                                           std::min(xt[0][x], yt[0][y])));
        p->green =
          static_cast<unsigned char>(tg - (gsign *
                                           std::min(xt[1][x], yt[1][y])));
        p->blue =
          static_cast<unsigned char>(tb - (bsign *
                                           std::min(xt[2][x], yt[2][y])));

        if (y & 1) {
          p->red   = (p->red   >> 1) + (p->red   >> 2);
          p->green = (p->green >> 1) + (p->green >> 2);
          p->blue  = (p->blue  >> 1) + (p->blue  >> 2);
        }
      }
    }
  }

  delete [] alloc;
}


void bt::Image::cdgradient(const Color &from, const Color &to,
                           bool interlaced) {
  // cross diagonal gradient -  based on original dgradient, written by
  // Mosfet (mosfet@kde.org)
  // adapted from kde sources for Blackbox by Brad Hughes

  double drx, dgx, dbx, dry, dgy, dby;
  double yr = 0.0, yg = 0.0, yb = 0.0,
         xr = static_cast<double>(from.red()  ),
         xg = static_cast<double>(from.green()),
         xb = static_cast<double>(from.blue() );
  RGB *p = data;
  unsigned int w = width * 2, h = height * 2;
  unsigned int x, y;

  const unsigned int dimension = std::max(width, height);
  unsigned int *alloc = new unsigned int[dimension * 6];
  unsigned int *xt[3], *yt[3];
  xt[0] = alloc + (dimension * 0);
  xt[1] = alloc + (dimension * 1);
  xt[2] = alloc + (dimension * 2);
  yt[0] = alloc + (dimension * 3);
  yt[1] = alloc + (dimension * 4);
  yt[2] = alloc + (dimension * 5);

  dry = drx = static_cast<double>(to.red()   - from.red()  );
  dgy = dgx = static_cast<double>(to.green() - from.green());
  dby = dbx = static_cast<double>(to.blue()  - from.blue() );

  // Create X table
  drx /= w;
  dgx /= w;
  dbx /= w;

  for (x = width - 1; x != 0; --x) {
    xt[0][x] = static_cast<unsigned char>(xr);
    xt[1][x] = static_cast<unsigned char>(xg);
    xt[2][x] = static_cast<unsigned char>(xb);

    xr += drx;
    xg += dgx;
    xb += dbx;
  }

  xt[0][x] = static_cast<unsigned char>(xr);
  xt[1][x] = static_cast<unsigned char>(xg);
  xt[2][x] = static_cast<unsigned char>(xb);

  // Create Y table
  dry /= h;
  dgy /= h;
  dby /= h;

  for (y = 0; y < height; ++y) {
    yt[0][y] = static_cast<unsigned char>(yr);
    yt[1][y] = static_cast<unsigned char>(yg);
    yt[2][y] = static_cast<unsigned char>(yb);

    yr += dry;
    yg += dgy;
    yb += dby;
  }

  // Combine tables to create gradient

  if (!interlaced) {
    // normal dgradient
    for (y = 0; y < height; ++y) {
      for (x = 0; x < width; ++x, ++p) {
        p->red   = xt[0][x] + yt[0][y];
        p->green = xt[1][x] + yt[1][y];
        p->blue  = xt[2][x] + yt[2][y];
      }
    }
  } else {
    // interlacing effect
    for (y = 0; y < height; ++y) {
      for (x = 0; x < width; ++x, ++p) {
        p->red   = xt[0][x] + yt[0][y];
        p->green = xt[1][x] + yt[1][y];
        p->blue  = xt[2][x] + yt[2][y];

        if (y & 1) {
          p->red   = (p->red   >> 1) + (p->red   >> 2);
          p->green = (p->green >> 1) + (p->green >> 2);
          p->blue  = (p->blue  >> 1) + (p->blue  >> 2);
        }
      }
    }
  }

  delete [] alloc;
}


/*
 * Adapted from a patch by David Barr, http://david.chalkskeletons.com
 * split grad: h1 -> from | to -> h2
 */
#define SAT_SHIFT(dest, input, shift) \
  dest = input; dest += input >> shift; if (dest > 0xff) dest = 0xff;
void bt::Image::svgradient(const Color &from, const Color &to,
                           bool interlaced)
{
  int rt, gt, bt;

  SAT_SHIFT(rt, from.red(),   2);
  SAT_SHIFT(gt, from.green(), 2);
  SAT_SHIFT(bt, from.blue(),  2);
  Color h1(rt, gt, bt);

  SAT_SHIFT(rt, to.red(),   4);
  SAT_SHIFT(gt, to.green(), 4);
  SAT_SHIFT(bt, to.blue(),  4);
  Color h2(rt, gt, bt);

  partial_vgradient(h1, from, interlaced, 0, height/2);
  partial_vgradient(to, h2, interlaced, height/2, height);
}

} // namespace bt
