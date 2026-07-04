// Classic rc value spellings for the Toolbar/Slit menu persist path - the
// writer side of what Config.cc parses case-insensitively. Header-only and
// pure: Config.cc is window-mgmt's file this wave, so the writer lives here and
// the menus persist through updateRcKey only. The keys are
// session.screen0.{enableToolbar,toolbar.placement,toolbar.autoHide,
// slit.placement,slit.direction,slit.autoHide} (enableToolbar is a top-level
// screen key, NOT toolbar.enableToolbar).
#ifndef BLACKBOXAI_BARSPELLING_HH
#define BLACKBOXAI_BARSPELLING_HH

#include "Config.hh"

namespace bbai::barmenu {

  inline const char *toolbarPlacementValue(toolbar::Placement p) {
    switch (p) {
    case toolbar::Placement::TopLeft:      return "TopLeft";
    case toolbar::Placement::TopCenter:    return "TopCenter";
    case toolbar::Placement::TopRight:     return "TopRight";
    case toolbar::Placement::BottomLeft:   return "BottomLeft";
    case toolbar::Placement::BottomRight:  return "BottomRight";
    case toolbar::Placement::BottomCenter: break;
    }
    return "BottomCenter";
  }

  inline const char *slitPlacementValue(SlitPlacement p) {
    switch (p) {
    case SlitPlacement::TopLeft:      return "TopLeft";
    case SlitPlacement::CenterLeft:   return "CenterLeft";
    case SlitPlacement::BottomLeft:   return "BottomLeft";
    case SlitPlacement::TopCenter:    return "TopCenter";
    case SlitPlacement::BottomCenter: return "BottomCenter";
    case SlitPlacement::TopRight:     return "TopRight";
    case SlitPlacement::BottomRight:  return "BottomRight";
    case SlitPlacement::CenterRight:  break;
    }
    return "CenterRight";
  }

  inline const char *slitDirectionValue(SlitDirection d) {
    return d == SlitDirection::Horizontal ? "Horizontal" : "Vertical";
  }

} // namespace bbai::barmenu

#endif // BLACKBOXAI_BARSPELLING_HH
