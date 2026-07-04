// Classic rc value spellings for the Configuration-menu persist path -
// BlackboxResource.cc:266-296 save() composes exactly these strings, and our
// Config parser (substring find for the focus composite, iequals for
// placement) reads them back. Header-only and pure on purpose: Config.cc is
// window-mgmt territory this wave, so the WRITER side of the spellings lives
// here and configmenu persists through updateRcKey only.
#ifndef BLACKBOXAI_CONFIGSPELLING_HH
#define BLACKBOXAI_CONFIGSPELLING_HH

#include "Config.hh"

#include <string>

namespace bbai::configmenu {

  inline std::string focusModelValue(const Config &cfg) {
    if (cfg.focusModel == FocusModel::ClickToFocus) return "ClickToFocus";
    std::string s = "SloppyFocus";
    if (cfg.autoRaise) s += " AutoRaise";
    if (cfg.clickRaise) s += " ClickRaise";
    return s;
  }

  inline const char *windowPlacementValue(WindowPlacement p) {
    switch (p) {
    case WindowPlacement::ColSmart: return "ColSmartPlacement";
    case WindowPlacement::Center:   return "CenterPlacement";
    case WindowPlacement::Cascade:  return "CascadePlacement";
    case WindowPlacement::RowSmart: break;
    }
    return "RowSmartPlacement";
  }

} // namespace bbai::configmenu

#endif // BLACKBOXAI_CONFIGSPELLING_HH
