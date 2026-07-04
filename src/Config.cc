// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Config.cc for BlackboxAI (Wayland)
//
// Decodes the well-known session resources into bbai::Config. Every read mirrors
// blackboxwm's BlackboxResource::load / ScreenResource::load: same key (and X
// class) names, same value spellings, same default. Enum spellings are matched
// case-insensitively (strcasecmp in the reference); the focus-model string is
// matched with substring find() and is case-sensitive, again per the reference.

#include "Config.hh"
#include "Util.hh"

#include <cctype>
#include <fstream>
#include <vector>

// Install-path defaults injected by src/meson.build; empty fallbacks keep
// stray compiles (and the unlikely no-define build) honest.
#ifndef BBAI_DEFAULT_STYLE
#define BBAI_DEFAULT_STYLE ""
#endif
#ifndef BBAI_DEFAULT_MENU
#define BBAI_DEFAULT_MENU ""
#endif

namespace {

  // Case-insensitive equality against a C string literal.
  bool iequals(const std::string &a, const char *b) {
    size_t i = 0;
    for (; i < a.size(); ++i) {
      if (!b[i]) return false;
      if (std::tolower(static_cast<unsigned char>(a[i])) !=
          std::tolower(static_cast<unsigned char>(b[i])))
        return false;
    }
    return b[i] == '\0';
  }

  // "session.screenN.<tail>" instance key.
  std::string screenName(unsigned screen, const char *tail) {
    return "session.screen" + std::to_string(screen) + "." + tail;
  }
  // "Session.screenN.<tail>" X-resource class key (reference capitalisation).
  std::string screenClass(unsigned screen, const char *tail) {
    return "Session.screen" + std::to_string(screen) + "." + tail;
  }

  // Split a workspaceNames value on ',', keeping empty fields (so a trailing
  // comma yields a trailing empty name) and preserving inner whitespace — the
  // exact behaviour of ScreenResource::load's std::find-based split. An empty
  // string means "no names", i.e. an empty vector.
  std::vector<std::string> splitNames(const std::string &s) {
    std::vector<std::string> out;
    if (s.empty()) return out;
    size_t start = 0;
    for (;;) {
      size_t comma = s.find(',', start);
      if (comma == std::string::npos) {
        out.push_back(s.substr(start));
        break;
      }
      out.push_back(s.substr(start, comma - start));
      start = comma + 1;
    }
    return out;
  }

} // namespace

namespace bbai {

  Config Config::fromResource(const bt::Resource &res, unsigned screen) {
    Config cfg;

    cfg.styleFile = bt::expandTilde(
      res.read("session.styleFile", "Session.StyleFile", BBAI_DEFAULT_STYLE));
    cfg.menuFile = bt::expandTilde(
      res.read("session.menuFile", "Session.MenuFile", BBAI_DEFAULT_MENU));
    cfg.rootCommand = res.read("rootCommand", "RootCommand", "");

    // --- focus model ---
    // session.focusModel falls back to the per-screen key, then "ClickToFocus".
    std::string fm =
      res.read("session.focusModel", "Session.FocusModel",
               res.read(screenName(screen, "focusModel"),
                        screenClass(screen, "FocusModel"),
                        "ClickToFocus"));
    if (fm.find("ClickToFocus") != std::string::npos) {
      cfg.focusModel = FocusModel::ClickToFocus;
      cfg.autoRaise = false;
      cfg.clickRaise = false;
    } else {
      cfg.focusModel = FocusModel::SloppyFocus;
      cfg.autoRaise = fm.find("AutoRaise") != std::string::npos;
      cfg.clickRaise = fm.find("ClickRaise") != std::string::npos;
    }

    cfg.focusNewWindows =
      res.read("session.focusNewWindows", "Session.FocusNewWindows",
               res.read(screenName(screen, "focusNewWindows"),
                        screenClass(screen, "FocusNewWindows"),
                        true));
    cfg.autoRaiseDelay =
      res.read("session.autoRaiseDelay", "Session.AutoRaiseDelay", 400);
    cfg.doubleClickInterval =
      res.read("session.doubleClickInterval", "Session.DoubleClickInterval", 250);

    // --- window placement ---
    std::string wp =
      res.read("session.windowPlacement", "Session.WindowPlacement",
               res.read(screenName(screen, "windowPlacement"),
                        screenClass(screen, "WindowPlacement"),
                        "RowSmartPlacement"));
    if (iequals(wp, "ColSmartPlacement"))
      cfg.windowPlacement = WindowPlacement::ColSmart;
    else if (iequals(wp, "CenterPlacement"))
      cfg.windowPlacement = WindowPlacement::Center;
    else if (iequals(wp, "CascadePlacement"))
      cfg.windowPlacement = WindowPlacement::Cascade;
    else
      cfg.windowPlacement = WindowPlacement::RowSmart;

    // --- per-screen workspaces ---
    cfg.workspaceCount =
      static_cast<unsigned>(res.read(screenName(screen, "workspaces"),
                                     screenClass(screen, "Workspaces"), 4));
    cfg.workspaceNames =
      splitNames(res.read(screenName(screen, "workspaceNames"),
                          screenClass(screen, "WorkspaceNames"), ""));

    // --- per-screen toolbar ---
    cfg.toolbar.enabled =
      res.read(screenName(screen, "enableToolbar"),
               screenClass(screen, "enableToolbar"), true);
    cfg.toolbar.widthPercent =
      res.read(screenName(screen, "toolbar.widthPercent"),
               screenClass(screen, "Toolbar.WidthPercent"), 66);
    cfg.toolbar.alwaysOnTop =
      res.read(screenName(screen, "toolbar.onTop"),
               screenClass(screen, "Toolbar.OnTop"), false);
    cfg.toolbar.autoHide =
      res.read(screenName(screen, "toolbar.autoHide"),
               screenClass(screen, "Toolbar.autoHide"), false);

    std::string tp = res.read(screenName(screen, "toolbar.placement"),
                              screenClass(screen, "Toolbar.Placement"),
                              "BottomCenter");
    if (iequals(tp, "TopLeft"))
      cfg.toolbar.placement = toolbar::Placement::TopLeft;
    else if (iequals(tp, "BottomLeft"))
      cfg.toolbar.placement = toolbar::Placement::BottomLeft;
    else if (iequals(tp, "TopCenter"))
      cfg.toolbar.placement = toolbar::Placement::TopCenter;
    else if (iequals(tp, "TopRight"))
      cfg.toolbar.placement = toolbar::Placement::TopRight;
    else if (iequals(tp, "BottomRight"))
      cfg.toolbar.placement = toolbar::Placement::BottomRight;
    else
      cfg.toolbar.placement = toolbar::Placement::BottomCenter;

    // --- per-screen slit + clock format ---
    cfg.strftimeFormat = res.read(screenName(screen, "strftimeFormat"),
                                  screenClass(screen, "StrftimeFormat"),
                                  "%I:%M %p");

    std::string sp = res.read(screenName(screen, "slit.placement"),
                              screenClass(screen, "Slit.Placement"),
                              "CenterRight");
    if (iequals(sp, "TopLeft"))            cfg.slit.placement = SlitPlacement::TopLeft;
    else if (iequals(sp, "CenterLeft"))    cfg.slit.placement = SlitPlacement::CenterLeft;
    else if (iequals(sp, "BottomLeft"))    cfg.slit.placement = SlitPlacement::BottomLeft;
    else if (iequals(sp, "TopCenter"))     cfg.slit.placement = SlitPlacement::TopCenter;
    else if (iequals(sp, "BottomCenter"))  cfg.slit.placement = SlitPlacement::BottomCenter;
    else if (iequals(sp, "TopRight"))      cfg.slit.placement = SlitPlacement::TopRight;
    else if (iequals(sp, "BottomRight"))   cfg.slit.placement = SlitPlacement::BottomRight;
    else                                   cfg.slit.placement = SlitPlacement::CenterRight;

    std::string sd = res.read(screenName(screen, "slit.direction"),
                              screenClass(screen, "Slit.Direction"),
                              "Vertical");
    cfg.slit.direction = iequals(sd, "Horizontal") ? SlitDirection::Horizontal
                                                   : SlitDirection::Vertical;
    cfg.slit.alwaysOnTop = res.read(screenName(screen, "slit.onTop"),
                                    screenClass(screen, "Slit.OnTop"), false);
    cfg.slit.autoHide = res.read(screenName(screen, "slit.autoHide"),
                                 screenClass(screen, "Slit.AutoHide"), false);

    return cfg;
  }

  Config Config::load(const std::string &filename, unsigned screen) {
    bt::Resource res(filename);  // unreadable file -> empty db -> all defaults
    return fromResource(res, screen);
  }

  bool updateRcKey(const std::string &rc_path, const std::string &key,
                   const std::string &value) {
    std::vector<std::string> lines;
    {
      std::ifstream in(rc_path);
      std::string line;
      while (std::getline(in, line)) lines.push_back(line);
    }
    const std::string entry = key + ": " + value;
    bool replaced = false;
    for (std::string &line : lines) {
      const size_t first = line.find_first_not_of(" \t");
      if (first == std::string::npos || line[first] == '!') continue;
      const size_t colon = line.find(':');
      if (colon == std::string::npos) continue;
      std::string k = line.substr(first, colon - first);
      while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
      if (k == key) { line = entry; replaced = true; break; }
    }
    if (!replaced) lines.push_back(entry);
    std::ofstream out(rc_path, std::ios::trunc);
    if (!out) return false;
    for (const std::string &line : lines) out << line << '\n';
    return static_cast<bool>(out);
  }

} // namespace bbai
