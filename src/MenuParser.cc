#include "MenuParser.hh"
#include "Text.hh"

#include <cctype>
#include <fstream>
#include <sstream>

namespace bbai::menuparser {

  namespace {

    // One field `<open> ... <close>` out of `line`, starting at `from`. Mirrors
    // blackbox's string_within: a backslash escapes the following character, the
    // `open` delimiter is consumed without emitting, and scanning stops at the
    // first `close`. `from` is advanced to the close delimiter (or end of line),
    // so the next field continues where this one left off. `found` is false when
    // `open` never appeared (an absent, not merely empty, field).
    std::string extractField(const std::string &line, std::size_t &from,
                             char open, char close, bool &found) {
      std::string out;
      bool parsing = false;
      std::size_t i = from;
      for (; i < line.size(); ++i) {
        char c = line[i];
        if (c == open) {
          parsing = true;
        } else if (c == close) {
          break;
        } else if (parsing) {
          if (c == '\\' && i + 1 < line.size())
            out.push_back(line[++i]);
          else
            out.push_back(c);
        }
      }
      from = i;
      found = parsing;
      return out;
    }

    // Lowercase + strip blanks so `[ Sub Menu ]` and `[submenu]` compare equal.
    std::string normalizeTag(const std::string &raw) {
      std::string t;
      for (char c : raw) {
        if (c == ' ' || c == '\t') continue;
        t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
      }
      return t;
    }

    std::vector<std::string> splitLines(const std::string &text) {
      std::vector<std::string> lines;
      std::string cur;
      std::istringstream in(text);
      while (std::getline(in, cur)) {
        if (!cur.empty() && cur.back() == '\r') cur.pop_back();  // CRLF
        lines.push_back(cur);
      }
      return lines;
    }

    std::string note(std::size_t lineNo, const std::string &msg) {
      return "line " + std::to_string(lineNo) + ": " + msg;
    }

    // Recursive-descent over `lines` from `idx`. Appends items to `out` until a
    // matching `[end]` or end-of-input. `title` is non-null only at the top
    // level, where the first `[begin]` sets it.
    void parseLevel(const std::vector<std::string> &lines, std::size_t &idx,
                    std::vector<MenuItem> &out, std::vector<std::string> &diag,
                    std::u32string *title) {
      while (idx < lines.size()) {
        const std::string &line = lines[idx];
        const std::size_t lineNo = idx + 1;
        ++idx;

        const std::size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;  // blank
        if (line[first] == '#') continue;          // comment

        std::size_t pos = 0;
        bool haveKw = false;
        const std::string kwRaw = extractField(line, pos, '[', ']', haveKw);
        if (!haveKw || kwRaw.empty()) continue;    // no [tag], not a menu line
        const std::string tag = normalizeTag(kwRaw);

        bool haveLabel = false, haveCmd = false;
        const std::string label = extractField(line, pos, '(', ')', haveLabel);
        const std::string cmd   = extractField(line, pos, '{', '}', haveCmd);

        if (tag == "end") {
          return;
        } else if (tag == "begin") {
          if (title && title->empty() && !label.empty())
            *title = bt::decodeUtf8(label.c_str());
          // [begin] opens no new nesting level; its [end] closes this one.
        } else if (tag == "sep" || tag == "separator") {
          MenuItem m;
          m.kind = MenuItem::Kind::Separator;
          out.push_back(std::move(m));
        } else if (tag == "nop") {
          MenuItem m;                              // inert spacer/label
          m.kind = MenuItem::Kind::Command;
          m.action = MenuItem::Act::None;
          m.enabled = false;
          if (haveLabel) m.label = bt::decodeUtf8(label.c_str());
          out.push_back(std::move(m));
        } else if (tag == "exec") {
          if (label.empty() || cmd.empty()) {
            diag.push_back(note(lineNo, "[exec] needs a label and a command - skipped"));
            continue;
          }
          MenuItem m;
          m.kind = MenuItem::Kind::Command;
          m.action = MenuItem::Act::Exec;
          m.label = bt::decodeUtf8(label.c_str());
          m.argv = {"/bin/sh", "-c", cmd};         // shell semantics, like bt::bexec
          out.push_back(std::move(m));
        } else if (tag == "exit") {
          if (label.empty()) {
            diag.push_back(note(lineNo, "[exit] needs a label - skipped"));
            continue;
          }
          MenuItem m;
          m.kind = MenuItem::Kind::Command;
          m.action = MenuItem::Act::Exit;
          m.label = bt::decodeUtf8(label.c_str());
          out.push_back(std::move(m));
        } else if (tag == "restart") {
          if (label.empty()) {
            diag.push_back(note(lineNo, "[restart] needs a label - skipped"));
            continue;
          }
          MenuItem m;
          m.kind = MenuItem::Kind::Command;
          m.label = bt::decodeUtf8(label.c_str());
          if (haveCmd && !cmd.empty()) {
            // Classic RestartOther: exec the named WM through the shell. On
            // Wayland this still tears every client down first - the
            // compositor IS the display server (documented in main.cc).
            m.action = MenuItem::Act::RestartOther;
            m.argv = {"/bin/sh", "-c", "exec " + cmd};
          } else {
            m.action = MenuItem::Act::Restart;   // restarts the compositor itself
          }
          out.push_back(std::move(m));
        } else if (tag == "workspaces" || tag == "config") {
          // Runtime-populated menus: the parser can't enumerate workspaces or
          // config options, so it emits an empty placeholder Submenu carrying
          // an Act marker (dead weight on Kind::Submenu - zero layout impact)
          // that the wire-up resolves at open time. [config] stays a disabled
          // placeholder until wave-2 configmenu mounts on it.
          if (label.empty()) {
            diag.push_back(note(lineNo, "[" + tag + "] needs a label - skipped"));
            continue;
          }
          MenuItem m;
          m.kind = MenuItem::Kind::Submenu;
          m.label = bt::decodeUtf8(label.c_str());
          m.action = (tag == "workspaces") ? MenuItem::Act::WorkspacesMenu
                                           : MenuItem::Act::ConfigMenu;
          if (tag == "config")
            diag.push_back(note(lineNo,
              "[config] menu is not populated yet - shown disabled"));
          out.push_back(std::move(m));
        } else if (tag == "submenu") {
          if (label.empty()) {
            diag.push_back(note(lineNo, "[submenu] needs a label - skipped (its body still parses)"));
            // Still consume the body so a label-less submenu can't desync [end]s.
            std::vector<MenuItem> drop;
            parseLevel(lines, idx, drop, diag, nullptr);
            continue;
          }
          MenuItem m;
          m.kind = MenuItem::Kind::Submenu;
          m.label = bt::decodeUtf8(label.c_str());
          if (haveCmd && !cmd.empty() && cmd != label)
            diag.push_back(note(lineNo,
              "[submenu] title '" + cmd +
              "' not stored (MenuItem has no submenu-title field; cascade uses the label)"));
          parseLevel(lines, idx, m.submenu_items, diag, nullptr);
          out.push_back(std::move(m));
        } else {
          diag.push_back(note(lineNo, "[" + tag + "] not supported - skipped"));
        }
      }
    }

  } // namespace

  Result parse(const std::string &text) {
    Result r;
    const std::vector<std::string> lines = splitLines(text);
    std::size_t idx = 0;
    parseLevel(lines, idx, r.items, r.diagnostics, &r.title);
    return r;
  }

  Result parseFile(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
      Result r;
      r.diagnostics.push_back("cannot open menu file: " + path);
      return r;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse(ss.str());
  }

} // namespace bbai::menuparser
