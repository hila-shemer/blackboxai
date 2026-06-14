// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Resource.hh for Blackbox - an X11 Window manager
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
// Ported to BlackboxAI (Wayland) from blackboxwm lib/Resource.hh:
// the XrmDatabase backend is replaced by a self-contained key/value parser
// over the classic Xrm file syntax (no libX11 link). Exact-match resolution
// is sufficient for M1 (full Xrm wildcard semantics deferred).

#ifndef BLACKBOXAI_RESOURCE_HH
#define BLACKBOXAI_RESOURCE_HH

#include <string>
#include <unordered_map>

namespace bt {

  inline const char *boolAsString(bool b) { return b ? "True" : "False"; }

  class Resource {
  public:
    Resource() = default;
    explicit Resource(const std::string &filename) { load(filename); }

    bool valid() const { return loaded; }
    void load(const std::string &filename);         // reads file, replaces db
    void loadFromString(const std::string &text);   // parse from memory (testable)

    std::string read(const std::string &name, const std::string &classname,
                     const std::string &default_value = std::string()) const;
    // A string-literal default would otherwise bind to the bool overload
    // (const char* -> bool beats the user-defined conversion to std::string),
    // so give it its own exact-match overload that returns a string.
    std::string read(const std::string &name, const std::string &classname,
                     const char *default_value) const;
    int  read(const std::string &name, const std::string &classname, int dflt) const;
    bool read(const std::string &name, const std::string &classname, bool dflt) const;

  private:
    void parseLine(const std::string &line);
    std::unordered_map<std::string, std::string> db;
    bool loaded = false;
  };

} // namespace bt

#endif // BLACKBOXAI_RESOURCE_HH
