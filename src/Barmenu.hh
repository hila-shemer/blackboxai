// The classic Toolbarmenu + Slitmenu as pure builders over the typed config.
// Rows carry configmenu's Act::ConfigOption idiom (one dispatch, one shared
// enum) - NOT per-knob Acts. onTop is omitted from BOTH menus (program law:
// no layer-aware restack, no lying toggle); the parsed *.onTop keys stay inert.
#ifndef BLACKBOXAI_BARMENU_HH
#define BLACKBOXAI_BARMENU_HH

#include "MenuItem.hh"
#include "Config.hh"

#include <vector>

namespace bbai::barmenu {

  std::vector<MenuItem> buildToolbar(const ToolbarConfig &tc);
  std::vector<MenuItem> buildSlit(const SlitConfig &sc);

} // namespace bbai::barmenu

#endif // BLACKBOXAI_BARMENU_HH
