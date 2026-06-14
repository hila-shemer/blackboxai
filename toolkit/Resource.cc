// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Ported to BlackboxAI (Wayland) from blackboxwm lib/Resource.cc:
// the XrmDatabase is replaced by a standalone parser over the classic Xrm
// file syntax (`Name.sub.class: value`, `!` line comments, blank lines,
// trailing-whitespace trim). `\`-continuation is ignored for M1.

#include "Resource.hh"

#include <fstream>
#include <sstream>
#include <cctype>

namespace {
  std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
  }
}

namespace bt {

  void Resource::parseLine(const std::string &raw) {
    std::string line = trim(raw);
    if (line.empty() || line[0] == '!') return;
    size_t colon = line.find(':');
    if (colon == std::string::npos) return;
    std::string key = trim(line.substr(0, colon));
    std::string val = trim(line.substr(colon + 1));
    if (!key.empty()) db[key] = val;
  }

  void Resource::loadFromString(const std::string &text) {
    db.clear();
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) parseLine(line);
    loaded = true;
  }

  void Resource::load(const std::string &filename) {
    std::ifstream f(filename);
    if (!f) { loaded = false; return; }
    std::stringstream ss;
    ss << f.rdbuf();
    loadFromString(ss.str());
  }

  std::string Resource::read(const std::string &name, const std::string &classname,
                             const std::string &dflt) const {
    auto it = db.find(name);
    if (it != db.end()) return it->second;
    if (!classname.empty()) {
      it = db.find(classname);
      if (it != db.end()) return it->second;
    }
    return dflt;
  }

  std::string Resource::read(const std::string &name, const std::string &classname,
                             const char *dflt) const {
    return read(name, classname, std::string(dflt ? dflt : ""));
  }

  int Resource::read(const std::string &name, const std::string &classname, int dflt) const {
    std::string v = read(name, classname, std::string());
    if (v.empty()) return dflt;
    try { return std::stoi(v); } catch (...) { return dflt; }
  }

  bool Resource::read(const std::string &name, const std::string &classname, bool dflt) const {
    std::string v = read(name, classname, std::string());
    if (v.empty()) return dflt;
    std::string lower;
    for (char c : v) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower == "true" || lower == "yes" || lower == "1";
  }

} // namespace bt
