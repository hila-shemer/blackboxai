// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Ported to BlackboxAI (Wayland) from blackboxwm lib/Resource.cc:
// the XrmDatabase is replaced by a standalone parser over the classic Xrm
// file syntax (`Name.sub.class: value`, `!` line comments, blank lines,
// trailing-whitespace trim). `\`-continuation is ignored for M1.

#include "Resource.hh"

#include <fstream>
#include <sstream>
#include <cctype>
#include <vector>

namespace {
  std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
  }

  std::vector<std::string> splitComponents(const std::string &key) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
      size_t dot = key.find('.', start);
      if (dot == std::string::npos) { out.push_back(key.substr(start)); break; }
      out.push_back(key.substr(start, dot - start));
      start = dot + 1;
    }
    return out;
  }

  // Split a wildcard pattern on '*' into literal component runs.
  // "toolbar*textColor" -> {{"toolbar"},{"textColor"}}; "*font" -> {{},{"font"}}.
  std::vector<std::vector<std::string>> splitPattern(const std::string &pattern) {
    std::vector<std::vector<std::string>> runs;
    size_t start = 0;
    for (;;) {
      size_t star = pattern.find('*', start);
      std::string seg = pattern.substr(
        start, star == std::string::npos ? std::string::npos : star - start);
      while (!seg.empty() && seg.front() == '.') seg.erase(seg.begin());
      while (!seg.empty() && seg.back() == '.') seg.pop_back();
      runs.push_back(seg.empty() ? std::vector<std::string>{} : splitComponents(seg));
      if (star == std::string::npos) break;
      start = star + 1;
    }
    return runs;
  }

  // '*' spans zero or more WHOLE components. runs[0] anchors at the start
  // (an empty first run == pattern begins with '*'); the last run must land
  // flush at the end.
  bool matchFrom(const std::vector<std::vector<std::string>> &runs, size_t ri,
                 const std::vector<std::string> &name, size_t ni, bool anchored) {
    if (ri == runs.size()) return ni == name.size();
    const std::vector<std::string> &run = runs[ri];
    const bool last = (ri + 1 == runs.size());
    auto runAt = [&](size_t off) {
      if (off + run.size() > name.size()) return false;
      for (size_t k = 0; k < run.size(); ++k)
        if (name[off + k] != run[k]) return false;
      return true;
    };
    if (anchored) {
      if (!runAt(ni)) return false;
      return matchFrom(runs, ri + 1, name, ni + run.size(), false);
    }
    for (size_t off = ni; off + run.size() <= name.size(); ++off) {
      if (last && off + run.size() != name.size()) continue;
      if (runAt(off) && matchFrom(runs, ri + 1, name, off + run.size(), false))
        return true;
    }
    return false;
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

  std::string Resource::wildcardRead(const std::string &key, bool &found) const {
    // Linear scan over wildcard keys - a style db is ~150 entries read ~100
    // times at load; not worth an index.
    const std::vector<std::string> comps = splitComponents(key);
    const std::string *best = nullptr;
    const std::string *best_key = nullptr;
    size_t best_lit = 0;
    for (const auto &kv : db) {
      if (kv.first.find('*') == std::string::npos) continue;
      if (!matchFrom(splitPattern(kv.first), 0, comps, 0, /*anchored=*/true))
        continue;
      size_t lit = 0;
      for (char c : kv.first) if (c != '*') ++lit;
      // Equal-literal ties fall to the lexicographically smallest pattern -
      // arbitrary but deterministic, where hash iteration order was
      // unspecified across stdlibs. (Xrm's per-component precedence could
      // differ; no shipped style has a co-matching equal-literal pair.)
      if (!best || lit > best_lit || (lit == best_lit && kv.first < *best_key)) {
        best = &kv.second;
        best_key = &kv.first;
        best_lit = lit;
      }
    }
    found = (best != nullptr);
    return best ? *best : std::string();
  }

  std::string Resource::read(const std::string &name, const std::string &classname,
                             const std::string &dflt) const {
    auto it = db.find(name);
    if (it != db.end()) return it->second;
    if (!classname.empty()) {
      it = db.find(classname);
      if (it != db.end()) return it->second;
    }
    bool found = false;
    std::string v = wildcardRead(name, found);
    if (found) return v;
    if (!classname.empty()) {
      v = wildcardRead(classname, found);
      if (found) return v;
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
