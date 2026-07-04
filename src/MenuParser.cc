#include "MenuParser.hh"
#include "Text.hh"
#include "Util.hh"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include <dirent.h>
#include <sys/stat.h>

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

    constexpr int kMaxIncludeDepth = 16;   // classic has NO cycle guard; we bound it

    // Threaded through the recursion: the injectable filesystem seams plus the
    // Result fields that are global to the parse (diagnostics, reload files).
    struct Ctx {
      const FileLoader &loader;
      const DirLister &lister;
      Result &result;
      int include_depth = 0;
    };

    // Recursive-descent over `lines` from `idx`. Appends items to `out` until a
    // matching `[end]` or end-of-input. `title` is non-null only at the top
    // level, where the first `[begin]` sets it.
    void parseLevel(const std::vector<std::string> &lines, std::size_t &idx,
                    std::vector<MenuItem> &out, Ctx &ctx, std::u32string *title) {
      std::vector<std::string> &diag = ctx.result.diagnostics;
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
          // that the wire-up resolves at open time.
          if (label.empty()) {
            diag.push_back(note(lineNo, "[" + tag + "] needs a label - skipped"));
            continue;
          }
          MenuItem m;
          m.kind = MenuItem::Kind::Submenu;
          m.label = bt::decodeUtf8(label.c_str());
          m.action = (tag == "workspaces") ? MenuItem::Act::WorkspacesMenu
                                           : MenuItem::Act::ConfigMenu;
          out.push_back(std::move(m));
        } else if (tag == "submenu") {
          if (label.empty()) {
            diag.push_back(note(lineNo, "[submenu] needs a label - skipped (its body still parses)"));
            // Still consume the body so a label-less submenu can't desync [end]s.
            std::vector<MenuItem> drop;
            parseLevel(lines, idx, drop, ctx, nullptr);
            continue;
          }
          MenuItem m;
          m.kind = MenuItem::Kind::Submenu;
          m.label = bt::decodeUtf8(label.c_str());
          if (haveCmd && !cmd.empty() && cmd != label)
            diag.push_back(note(lineNo,
              "[submenu] title '" + cmd +
              "' not stored (MenuItem has no submenu-title field; cascade uses the label)"));
          parseLevel(lines, idx, m.submenu_items, ctx, nullptr);
          out.push_back(std::move(m));
        } else if (tag == "include") {
          if (label.empty()) {
            diag.push_back(note(lineNo, "[include] needs a filename - skipped"));
            continue;
          }
          if (label[0] == '|') {
            // popen at menu-open would block the compositor loop - explicit v1
            // non-goal (program plan, locked).
            diag.push_back(note(lineNo, "[include] pipe menus are not supported - skipped"));
            continue;
          }
          if (ctx.include_depth >= kMaxIncludeDepth) {
            diag.push_back(note(lineNo, "[include] nested too deep (cycle?) - skipped"));
            continue;
          }
          const std::string path = bt::expandTilde(label);
          std::string text;
          if (!ctx.loader(path, text)) {
            diag.push_back(note(lineNo, "[include] cannot read '" + path + "' - skipped"));
            continue;
          }
          ctx.result.files.push_back(path);
          // Classic parseMenuFile(file, menu): included items append into the
          // CURRENT level (works inside a submenu). A [begin] inside the
          // included file is ignored; a stray top-level [end] there stops that
          // include's remainder only.
          const std::vector<std::string> inc = splitLines(text);
          std::size_t j = 0;
          ++ctx.include_depth;
          parseLevel(inc, j, out, ctx, nullptr);
          --ctx.include_depth;
        } else if (tag == "style") {
          if (label.empty() || cmd.empty()) {
            diag.push_back(note(lineNo, "[style] needs a label and a filename - skipped"));
            continue;
          }
          MenuItem m;
          m.kind = MenuItem::Kind::Command;
          m.action = MenuItem::Act::SetStyle;
          m.label = bt::decodeUtf8(label.c_str());
          m.argv = {bt::expandTilde(cmd)};
          out.push_back(std::move(m));
        } else if (tag == "reconfig") {
          if (label.empty()) {
            diag.push_back(note(lineNo, "[reconfig] needs a label - skipped"));
            continue;
          }
          MenuItem m;
          m.kind = MenuItem::Kind::Command;
          m.action = MenuItem::Act::Reconfigure;
          m.label = bt::decodeUtf8(label.c_str());
          if (haveCmd && !cmd.empty())
            diag.push_back(note(lineNo,
              "[reconfig] command '" + cmd + "' dropped (classic ignores it too - the man page lies)"));
          out.push_back(std::move(m));
        } else if (tag == "stylesdir" || tag == "stylesmenu") {
          // One impl for both, like classic: stylesdir takes the directory as
          // its (label) and inlines; stylesmenu takes (label){dir} and wraps
          // the listing in a titled submenu.
          const bool newmenu = (tag == "stylesmenu");
          if (label.empty() || (newmenu && cmd.empty())) {
            diag.push_back(note(lineNo, "[" + tag + "] needs a directory - skipped"));
            continue;
          }
          const std::string dir = bt::expandTilde(newmenu ? cmd : label);
          std::vector<std::string> names;
          if (!ctx.lister(dir, names)) {
            diag.push_back(note(lineNo, "[" + tag + "] cannot list '" + dir + "' - skipped"));
            continue;
          }
          ctx.result.files.push_back(dir);   // dir edits re-trigger the reload
          std::sort(names.begin(), names.end());
          std::vector<MenuItem> styles;
          for (const std::string &name : names) {
            if (name.empty() || name[0] == '.' || name.back() == '~')
              continue;                       // dotfiles + editor backups
            MenuItem s;
            s.kind = MenuItem::Kind::Command;
            s.action = MenuItem::Act::SetStyle;
            s.argv = {dir + "/" + name};
            std::string display = name;
            std::replace(display.begin(), display.end(), '_', ' ');  // This_Name -> This Name
            s.label = bt::decodeUtf8(display.c_str());
            styles.push_back(std::move(s));
          }
          if (newmenu) {
            MenuItem m;
            m.kind = MenuItem::Kind::Submenu;
            m.label = bt::decodeUtf8(label.c_str());
            m.submenu_items = std::move(styles);
            out.push_back(std::move(m));
          } else {
            for (MenuItem &s : styles) out.push_back(std::move(s));
          }
        } else {
          diag.push_back(note(lineNo, "[" + tag + "] not supported - skipped"));
        }
      }
    }

  } // namespace

  FileLoader defaultFileLoader() {
    return [](const std::string &path, std::string &text) {
      struct stat st;
      if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        return false;                        // classic [include] S_ISREG check
      std::ifstream f(path, std::ios::binary);
      if (!f) return false;
      std::ostringstream ss;
      ss << f.rdbuf();
      text = ss.str();
      return true;
    };
  }

  DirLister defaultDirLister() {
    return [](const std::string &dir, std::vector<std::string> &names) {
      DIR *d = opendir(dir.c_str());
      if (!d) return false;
      while (dirent *p = readdir(d)) {
        const std::string name = p->d_name;
        if (name == "." || name == "..") continue;
        struct stat st;
        if (stat((dir + "/" + name).c_str(), &st) == 0 && S_ISREG(st.st_mode))
          names.push_back(name);
      }
      closedir(d);
      return true;
    };
  }

  Result parse(const std::string &text,
               const FileLoader &loader, const DirLister &lister) {
    Result r;
    Ctx ctx{loader, lister, r, 0};
    const std::vector<std::string> lines = splitLines(text);
    std::size_t idx = 0;
    parseLevel(lines, idx, r.items, ctx, &r.title);
    return r;
  }

  Result parseFile(const std::string &path,
                   const FileLoader &loader, const DirLister &lister) {
    std::string text;
    if (!loader(path, text)) {
      Result r;
      r.diagnostics.push_back("cannot open menu file: " + path);
      return r;
    }
    Result r = parse(text, loader, lister);
    r.files.insert(r.files.begin(), path);   // main file first, includes after
    return r;
  }

} // namespace bbai::menuparser
