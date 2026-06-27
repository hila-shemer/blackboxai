// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Config.cc for BlackboxAI (Wayland)
//
// Decodes the well-known session resources into bbai::Config. Every read mirrors
// blackboxwm's BlackboxResource::load / ScreenResource::load: same key (and X
// class) names, same value spellings, same default. Enum spellings are matched
// case-insensitively (strcasecmp in the reference); the focus-model string is
// matched with substring find() and is case-sensitive, again per the reference.

#include "Config.hh"

#include <cctype>

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
      cfg.toolbar.placement = ToolbarPlacement::TopLeft;
    else if (iequals(tp, "BottomLeft"))
      cfg.toolbar.placement = ToolbarPlacement::BottomLeft;
    else if (iequals(tp, "TopCenter"))
      cfg.toolbar.placement = ToolbarPlacement::TopCenter;
    else if (iequals(tp, "TopRight"))
      cfg.toolbar.placement = ToolbarPlacement::TopRight;
    else if (iequals(tp, "BottomRight"))
      cfg.toolbar.placement = ToolbarPlacement::BottomRight;
    else
      cfg.toolbar.placement = ToolbarPlacement::BottomCenter;

    return cfg;
  }

  Config Config::load(const std::string &filename, unsigned screen) {
    bt::Resource res(filename);  // unreadable file -> empty db -> all defaults
    return fromResource(res, screen);
  }

} // namespace bbai
