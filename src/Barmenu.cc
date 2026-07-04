#include "Barmenu.hh"
#include "Text.hh"

namespace bbai::barmenu {

  namespace {
    MenuItem option(const char *label, ConfigOption opt, bool checked, bool enabled = true) {
      MenuItem m;
      m.label = bt::decodeUtf8(label);
      m.action = MenuItem::Act::ConfigOption;
      m.option = opt;
      m.checked = checked;
      m.enabled = enabled;
      return m;
    }
    MenuItem separator() { MenuItem m; m.kind = MenuItem::Kind::Separator; return m; }
    MenuItem submenu(const char *label, std::vector<MenuItem> kids) {
      MenuItem m;
      m.kind = MenuItem::Kind::Submenu;
      m.label = bt::decodeUtf8(label);
      m.submenu_items = std::move(kids);
      return m;
    }
  } // namespace

  std::vector<MenuItem> buildToolbar(const ToolbarConfig &tc) {
    using P = toolbar::Placement;
    std::vector<MenuItem> place;
    auto row = [&](const char *l, ConfigOption o, P p) {
      place.push_back(option(l, o, tc.placement == p));
    };
    row("Top Left",      ConfigOption::ToolbarPlaceTopLeft,      P::TopLeft);
    row("Top Center",    ConfigOption::ToolbarPlaceTopCenter,    P::TopCenter);
    row("Top Right",     ConfigOption::ToolbarPlaceTopRight,     P::TopRight);
    row("Bottom Left",   ConfigOption::ToolbarPlaceBottomLeft,   P::BottomLeft);
    row("Bottom Center", ConfigOption::ToolbarPlaceBottomCenter, P::BottomCenter);
    row("Bottom Right",  ConfigOption::ToolbarPlaceBottomRight,  P::BottomRight);

    std::vector<MenuItem> items;
    items.push_back(option("Enable Toolbar", ConfigOption::ToolbarEnabled, tc.enabled));
    items.push_back(separator());
    items.push_back(submenu("Placement", std::move(place)));
    items.push_back(option("Auto Hide", ConfigOption::ToolbarAutoHide, tc.autoHide));
    return items;
  }

  std::vector<MenuItem> buildSlit(const SlitConfig &sc) {
    std::vector<MenuItem> dir;
    dir.push_back(option("Horizontal", ConfigOption::SlitDirHorizontal,
                         sc.direction == SlitDirection::Horizontal));
    dir.push_back(option("Vertical", ConfigOption::SlitDirVertical,
                         sc.direction == SlitDirection::Vertical));

    using S = SlitPlacement;
    std::vector<MenuItem> place;
    auto row = [&](const char *l, ConfigOption o, S p) {
      place.push_back(option(l, o, sc.placement == p));
    };
    row("Top Left",      ConfigOption::SlitPlaceTopLeft,      S::TopLeft);
    row("Center Left",   ConfigOption::SlitPlaceCenterLeft,   S::CenterLeft);
    row("Bottom Left",   ConfigOption::SlitPlaceBottomLeft,   S::BottomLeft);
    row("Top Center",    ConfigOption::SlitPlaceTopCenter,    S::TopCenter);
    row("Bottom Center", ConfigOption::SlitPlaceBottomCenter, S::BottomCenter);
    row("Top Right",     ConfigOption::SlitPlaceTopRight,     S::TopRight);
    row("Center Right",  ConfigOption::SlitPlaceCenterRight,  S::CenterRight);
    row("Bottom Right",  ConfigOption::SlitPlaceBottomRight,  S::BottomRight);

    std::vector<MenuItem> items;
    items.push_back(submenu("Direction", std::move(dir)));
    items.push_back(submenu("Placement", std::move(place)));
    items.push_back(option("Auto Hide", ConfigOption::SlitAutoHide, sc.autoHide));
    return items;
  }

} // namespace bbai::barmenu
