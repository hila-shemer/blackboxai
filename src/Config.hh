// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
// Config.hh for BlackboxAI (Wayland)
//
// M5 typed-config parse layer: a `bbai::Config` is the well-known session
// resources of a classic `~/.blackboxrc`, decoded into typed fields on top of
// the low-level bt::Resource key/value parser. The key names, value spellings
// and defaults mirror blackboxwm's BlackboxResource/ScreenResource::load so an
// existing config Just Works.
//
// Scope is the parse layer only: this struct is filled from a file and nothing
// here touches the running compositor. The covered knobs are the daily-driver
// ones (focus model, focus-new-windows, auto-raise, window placement,
// double-click interval, workspace count/names, toolbar placement/auto-hide/
// width, style/menu file paths, rootCommand, strftimeFormat, slit anchor).
// Dithering, mouse-wheel and snap-threshold resources stay out of scope.

#ifndef BLACKBOXAI_CONFIG_HH
#define BLACKBOXAI_CONFIG_HH

#include "Resource.hh"
#include "Toolbar.geom.hh"   // bbai::toolbar::Placement - the one placement enum

#include <string>
#include <vector>

namespace bbai {

  // SloppyFocus == focus-follows-mouse; ClickToFocus is the reference default.
  enum class FocusModel { ClickToFocus, SloppyFocus };

  // Window auto-placement policy (session.windowPlacement).
  enum class WindowPlacement { RowSmart, ColSmart, Center, Cascade };

  // Slit anchor/direction (session.screenN.slit.*). Parsed now so the wave-2
  // slit slice reads typed values and never touches this file.
  enum class SlitPlacement {
    TopLeft, CenterLeft, BottomLeft, TopCenter, BottomCenter,
    TopRight, CenterRight, BottomRight
  };
  enum class SlitDirection { Vertical, Horizontal };

  struct ToolbarConfig {
    bool enabled = true;                                  // enableToolbar
    toolbar::Placement placement = toolbar::Placement::BottomCenter;
    bool alwaysOnTop = false;                             // toolbar.onTop
    bool autoHide = false;                                // toolbar.autoHide
    int widthPercent = 66;                                // toolbar.widthPercent
  };

  struct SlitConfig {
    SlitPlacement placement = SlitPlacement::CenterRight;
    SlitDirection direction = SlitDirection::Vertical;
    bool alwaysOnTop = false;
    bool autoHide = false;
  };

  struct Config {
    // --- session file paths (tilde-expanded) ---
    std::string styleFile;   // session.styleFile; default BBAI_DEFAULT_STYLE
    std::string menuFile;    // session.menuFile;  default BBAI_DEFAULT_MENU

    // rc-file rootCommand - user-authored, run via /bin/sh by the Server.
    // (The STYLE file's rootCommand never reaches a shell - see Style.)
    std::string rootCommand;

    // --- focus model (session.focusModel and its sub-flags) ---
    FocusModel focusModel = FocusModel::ClickToFocus;
    bool autoRaise = false;
    bool clickRaise = false;
    bool focusNewWindows = true;   // session.focusNewWindows (reference: True)
    int autoRaiseDelay = 400;
    int doubleClickInterval = 250;

    // --- window placement (session.windowPlacement) ---
    WindowPlacement windowPlacement = WindowPlacement::RowSmart;

    // --- per-screen workspaces (session.screenN.*) ---
    unsigned workspaceCount = 4;
    std::vector<std::string> workspaceNames;

    // --- per-screen toolbar / slit / clock format ---
    ToolbarConfig toolbar;
    SlitConfig slit;
    std::string strftimeFormat = "%I:%M %p";  // parse-only this wave (locked)

    static Config fromResource(const bt::Resource &res, unsigned screen = 0);
    static Config load(const std::string &filename, unsigned screen = 0);
  };

  // Rewrite ONE key in an rc file, preserving everything else the user wrote
  // (classic Resource::merge saved the whole db and reformatted the file; this
  // is deliberately narrower). Replaces the first "key:" line - comments
  // excluded - or appends; creates the file if missing. False on I/O failure.
  bool updateRcKey(const std::string &rc_path, const std::string &key,
                   const std::string &value);

} // namespace bbai

#endif // BLACKBOXAI_CONFIG_HH
