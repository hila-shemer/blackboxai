#include "Autostart.hh"

#include <algorithm>

namespace bbai {

  namespace {
    // Split a ';'-terminated desktop list ("GNOME;Blackbox;") into its values,
    // dropping the empty token after the trailing ';'.
    std::vector<std::string> splitList(const std::string &v) {
      std::vector<std::string> out;
      std::string cur;
      for (char c : v) {
        if (c == ';') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
      }
      if (!cur.empty()) out.push_back(cur);
      return out;
    }

    bool contains(const std::vector<std::string> &v, const std::string &s) {
      return std::find(v.begin(), v.end(), s) != v.end();
    }
  }

  DesktopEntry parseDesktopEntry(const std::string &contents) {
    DesktopEntry e;
    bool in_group = false;
    std::string line;
    auto consume = [&](const std::string &raw) {
      // Trim a trailing '\r' so CRLF files parse; leading/inner space is kept
      // (Exec values can legitimately contain spaces).
      std::string l = raw;
      if (!l.empty() && l.back() == '\r') l.pop_back();
      if (l.empty() || l[0] == '#') return;
      if (l[0] == '[') {                       // a group header
        in_group = (l == "[Desktop Entry]");
        return;
      }
      if (!in_group) return;
      const auto eq = l.find('=');
      if (eq == std::string::npos) return;
      const std::string key = l.substr(0, eq);
      const std::string val = l.substr(eq + 1);
      if (key == "Exec") e.exec = val;
      else if (key == "TryExec") e.try_exec = val;
      else if (key == "Hidden") e.hidden = (val == "true");
      else if (key == "OnlyShowIn") e.only_show_in = splitList(val);
      else if (key == "NotShowIn") e.not_show_in = splitList(val);
    };
    for (char c : contents) {
      if (c == '\n') { consume(line); line.clear(); }
      else line += c;
    }
    consume(line);   // last line need not be newline-terminated
    return e;
  }

  bool shouldAutostart(const DesktopEntry &e, const std::string &current_desktop) {
    if (e.hidden) return false;
    if (!e.only_show_in.empty() && !contains(e.only_show_in, current_desktop)) return false;
    if (contains(e.not_show_in, current_desktop)) return false;
    return true;
  }

  std::string stripFieldCodes(const std::string &exec) {
    static const std::string codes = "fFuUickdDnNvm";
    std::string out;
    for (size_t i = 0; i < exec.size(); ++i) {
      if (exec[i] == '%' && i + 1 < exec.size()) {
        const char nxt = exec[i + 1];
        if (nxt == '%') { out += '%'; ++i; continue; }            // literal percent
        if (codes.find(nxt) != std::string::npos) { ++i; continue; }  // drop the code
      }
      out += exec[i];
    }
    // Collapse runs of spaces left by the dropped codes, then trim the ends.
    std::string collapsed;
    bool prev_space = false;
    for (char c : out) {
      if (c == ' ') { if (!prev_space) collapsed += c; prev_space = true; }
      else { collapsed += c; prev_space = false; }
    }
    const size_t b = collapsed.find_first_not_of(' ');
    if (b == std::string::npos) return "";
    const size_t e = collapsed.find_last_not_of(' ');
    return collapsed.substr(b, e - b + 1);
  }

} // namespace bbai
