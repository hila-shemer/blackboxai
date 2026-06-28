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
// width). Style/menu file paths, dithering, slit, mouse-wheel and snap-threshold
// resources are deliberately out of scope here.

#ifndef BLACKBOXAI_CONFIG_HH
#define BLACKBOXAI_CONFIG_HH

#include "Resource.hh"

#include <string>
#include <vector>

namespace bbai {

  // SloppyFocus == focus-follows-mouse; ClickToFocus is the reference default.
  enum class FocusModel { ClickToFocus, SloppyFocus };

  // Window auto-placement policy (session.windowPlacement).
  enum class WindowPlacement { RowSmart, ColSmart, Center, Cascade };

  // Toolbar anchor (session.screenN.toolbar.placement). The toolbar has no
  // Center{Left,Right} edges (those are slit-only), hence the six values.
  enum class ToolbarPlacement {
    TopLeft, TopCenter, TopRight,
    BottomLeft, BottomCenter, BottomRight
  };

  struct ToolbarConfig {
    bool enabled = true;                                  // enableToolbar
    ToolbarPlacement placement = ToolbarPlacement::BottomCenter;
    bool alwaysOnTop = false;                             // toolbar.onTop
    bool autoHide = false;                                // toolbar.autoHide
    int widthPercent = 66;                                // toolbar.widthPercent
  };

  struct Config {
    // --- focus model (session.focusModel and its sub-flags) ---
    FocusModel focusModel = FocusModel::ClickToFocus;
    bool autoRaise = false;   // SloppyFocus + "AutoRaise"; forced off under Click
    bool clickRaise = false;  // SloppyFocus + "ClickRaise"; forced off under Click
    bool focusNewWindows = true;   // session.focusNewWindows (reference: True)
    int autoRaiseDelay = 400;      // session.autoRaiseDelay, milliseconds
    int doubleClickInterval = 250; // session.doubleClickInterval, milliseconds

    // --- window placement (session.windowPlacement) ---
    WindowPlacement windowPlacement = WindowPlacement::RowSmart;

    // --- per-screen workspaces (session.screenN.*) ---
    unsigned workspaceCount = 4;
    std::vector<std::string> workspaceNames;  // UTF-8; empty == "use defaults"

    // --- per-screen toolbar (session.screenN.toolbar.*) ---
    ToolbarConfig toolbar;

    // Decode from an already-parsed resource db. `screen` selects the
    // session.screenN.* block for the per-screen knobs.
    static Config fromResource(const bt::Resource &res, unsigned screen = 0);

    // Read `filename` via bt::Resource, then fromResource(). A missing or
    // unreadable file is not an error: every field takes its reference default.
    static Config load(const std::string &filename, unsigned screen = 0);
  };

} // namespace bbai

#endif // BLACKBOXAI_CONFIG_HH
