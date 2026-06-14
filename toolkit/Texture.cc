// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Texture.cc for Blackbox - an X11 Window manager
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
// Ported to BlackboxAI (Wayland) from blackboxwm lib/Texture.cc:
// the Texture class (setColor1() light/shadow derivation, setDescription()
// appearance-string parser, operator=) is kept verbatim. The X drawing free
// functions drawTexture() and textureResource() are dropped — drawing is done
// by Image::renderBuffer(), and resource-driven construction returns in a
// later milestone.

#include "Texture.hh"
#include <cctype>
#include <string>


static std::string toLower(const std::string &s) {
  std::string r; r.reserve(s.size());
  for (char c : s) r += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return r;
}


void bt::Texture::setColor1(const bt::Color &new_color) {
  c1 = new_color;

  unsigned char r, g, b, rr, gg, bb;
  r = c1.red();
  g = c1.green();
  b = c1.blue();

  // calculate the light color
  rr = r + (r >> 1);
  gg = g + (g >> 1);
  bb = b + (b >> 1);
  if (rr < r)
    rr = ~0;
  if (gg < g)
    gg = ~0;
  if (bb < b)
    bb = ~0;
  lc.setRGB(rr, gg, bb);

  // calculate the shadow color
  rr = (r >> 2) + (r >> 1);
  gg = (g >> 2) + (g >> 1);
  bb = (b >> 2) + (b >> 1);
  if (rr > r)
    rr = 0;
  if (gg > g)
    gg = 0;
  if (bb > b)
    bb = 0;
  sc.setRGB(rr, gg, bb);
}


void bt::Texture::setDescription(const std::string &d) {
  descr = toLower(d);
  if (descr.find("parentrelative") != std::string::npos) {
    setTexture(bt::Texture::Parent_Relative);
  } else {
    setTexture(0);

    if (descr.find("gradient") != std::string::npos) {
      addTexture(bt::Texture::Gradient);
      if (descr.find("crossdiagonal") != std::string::npos)
        addTexture(bt::Texture::CrossDiagonal);
      else if (descr.find("rectangle") != std::string::npos)
        addTexture(bt::Texture::Rectangle);
      else if (descr.find("pyramid") != std::string::npos)
        addTexture(bt::Texture::Pyramid);
      else if (descr.find("pipecross") != std::string::npos)
        addTexture(bt::Texture::PipeCross);
      else if (descr.find("elliptic") != std::string::npos)
        addTexture(bt::Texture::Elliptic);
      else if (descr.find("horizontal") != std::string::npos)
        addTexture(bt::Texture::Horizontal);
      else if (descr.find("splitvertical") != std::string::npos)
        addTexture(bt::Texture::SplitVertical);
      else if (descr.find("vertical") != std::string::npos)
        addTexture(bt::Texture::Vertical);
      else
        addTexture(bt::Texture::Diagonal);
    } else {
      addTexture(bt::Texture::Solid);
    }

    if (descr.find("sunken") != std::string::npos)
      addTexture(bt::Texture::Sunken);
    else if (descr.find("flat") != std::string::npos)
      addTexture(bt::Texture::Flat);
    else
      addTexture(bt::Texture::Raised);

    if (descr.find("interlaced") != std::string::npos)
      addTexture(bt::Texture::Interlaced);

    if (descr.find("border") != std::string::npos)
      addTexture(bt::Texture::Border);
  }
}


bt::Texture& bt::Texture::operator=(const bt::Texture &tt) {
  descr = tt.descr;

  c1 = tt.c1;
  c2 = tt.c2;
  bc = tt.bc;
  lc = tt.lc;
  sc = tt.sc;
  t  = tt.t;
  bw = tt.bw;

  return *this;
}
