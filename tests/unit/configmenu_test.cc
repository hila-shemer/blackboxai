// Pure Configuration-submenu build: the checked/enabled matrix mirrors classic
// Configmenu::refresh (reference/blackboxwm/src/Configmenu.cc:242-253,319-347).
// No wlroots, no rendering. Config fields are always set EXPLICITLY here -
// window-mgmt owns (and flips) the focus-model default, and this file must be
// merge-stable across that.
#include <doctest/doctest.h>
#include "Rootmenu.hh"
#include "Config.hh"
#include "ConfigSpelling.hh"
#include "Text.hh"

#include <cstdio>

using namespace bbai;

TEST_CASE("shape: Focus Model + Window Placement submenus, separator, Focus New Windows") {
  Config cfg;
  auto items = rootmenu::buildConfigSubmenu(cfg);
  REQUIRE(items.size() == 4);
  CHECK(items[0].kind == MenuItem::Kind::Submenu);
  CHECK(items[0].label == bt::decodeUtf8("Focus Model"));
  REQUIRE(items[0].submenu_items.size() == 4);
  CHECK(items[1].kind == MenuItem::Kind::Submenu);
  CHECK(items[1].label == bt::decodeUtf8("Window Placement"));
  REQUIRE(items[1].submenu_items.size() == 4);
  CHECK(items[2].separator());
  CHECK(items[3].action == MenuItem::Act::ConfigOption);
  CHECK(items[3].option == ConfigOption::FocusNewWindows);
  CHECK(items[3].label == bt::decodeUtf8("Focus New Windows"));
}

TEST_CASE("ClickToFocus: CTF radio checked, raise rows disabled (classic sloppy-only enabling)") {
  Config cfg;
  cfg.focusModel = FocusModel::ClickToFocus;
  cfg.autoRaise = false;
  cfg.clickRaise = false;
  auto fm = rootmenu::buildConfigSubmenu(cfg)[0].submenu_items;
  CHECK(fm[0].checked);
  CHECK(fm[0].enabled);
  CHECK(fm[0].option == ConfigOption::FocusClickToFocus);
  CHECK_FALSE(fm[1].checked);
  CHECK(fm[1].option == ConfigOption::FocusSloppy);
  CHECK_FALSE(fm[2].enabled);
  CHECK_FALSE(fm[2].checked);
  CHECK_FALSE(fm[3].enabled);
  CHECK_FALSE(fm[3].checked);
}

TEST_CASE("SloppyFocus + AutoRaise: radios exclusive, raise rows enabled and checked from cfg") {
  Config cfg;
  cfg.focusModel = FocusModel::SloppyFocus;
  cfg.autoRaise = true;
  cfg.clickRaise = false;
  auto fm = rootmenu::buildConfigSubmenu(cfg)[0].submenu_items;
  CHECK_FALSE(fm[0].checked);
  CHECK(fm[1].checked);
  CHECK(fm[2].enabled);
  CHECK(fm[2].checked);
  CHECK(fm[2].option == ConfigOption::AutoRaise);
  CHECK(fm[3].enabled);
  CHECK_FALSE(fm[3].checked);
  CHECK(fm[3].option == ConfigOption::ClickRaise);
}

TEST_CASE("placement radios: exactly the configured policy is checked, all rows enabled") {
  Config cfg;
  cfg.windowPlacement = WindowPlacement::Cascade;
  auto wp = rootmenu::buildConfigSubmenu(cfg)[1].submenu_items;
  CHECK_FALSE(wp[0].checked);
  CHECK_FALSE(wp[1].checked);
  CHECK_FALSE(wp[2].checked);
  CHECK(wp[3].checked);
  for (const MenuItem &m : wp) CHECK(m.enabled);
  CHECK(wp[0].option == ConfigOption::PlacementRowSmart);
  CHECK(wp[1].option == ConfigOption::PlacementColSmart);
  CHECK(wp[2].option == ConfigOption::PlacementCenter);
  CHECK(wp[3].option == ConfigOption::PlacementCascade);
}

TEST_CASE("Focus New Windows mirrors cfg both ways") {
  Config cfg;
  cfg.focusNewWindows = false;
  CHECK_FALSE(rootmenu::buildConfigSubmenu(cfg)[3].checked);
  cfg.focusNewWindows = true;
  CHECK(rootmenu::buildConfigSubmenu(cfg)[3].checked);
}

TEST_CASE("persist spellings: composite focusModel round-trips through the parser") {
  Config cfg;
  cfg.focusModel = FocusModel::SloppyFocus;
  cfg.autoRaise = true;
  cfg.clickRaise = true;
  CHECK(configmenu::focusModelValue(cfg) == "SloppyFocus AutoRaise ClickRaise");

  cfg.clickRaise = false;
  CHECK(configmenu::focusModelValue(cfg) == "SloppyFocus AutoRaise");

  const char *p = "/tmp/bbai-configmenu-spelling.rc";
  std::remove(p);
  cfg.clickRaise = true;
  REQUIRE(bbai::updateRcKey(p, "session.focusModel", configmenu::focusModelValue(cfg)));
  Config re = Config::load(p);
  CHECK(re.focusModel == FocusModel::SloppyFocus);
  CHECK(re.autoRaise);
  CHECK(re.clickRaise);

  cfg.focusModel = FocusModel::ClickToFocus;
  CHECK(configmenu::focusModelValue(cfg) == "ClickToFocus");
  REQUIRE(bbai::updateRcKey(p, "session.focusModel", configmenu::focusModelValue(cfg)));
  re = Config::load(p);
  CHECK(re.focusModel == FocusModel::ClickToFocus);
  CHECK_FALSE(re.autoRaise);   // the parser forces both raise flags off under CTF
  CHECK_FALSE(re.clickRaise);
  std::remove(p);
}

TEST_CASE("persist spellings: windowPlacement strings round-trip") {
  using WP = WindowPlacement;
  CHECK(std::string(configmenu::windowPlacementValue(WP::RowSmart)) == "RowSmartPlacement");
  CHECK(std::string(configmenu::windowPlacementValue(WP::ColSmart)) == "ColSmartPlacement");
  CHECK(std::string(configmenu::windowPlacementValue(WP::Center)) == "CenterPlacement");
  CHECK(std::string(configmenu::windowPlacementValue(WP::Cascade)) == "CascadePlacement");

  const char *p = "/tmp/bbai-configmenu-spelling.rc";
  std::remove(p);
  REQUIRE(bbai::updateRcKey(p, "session.windowPlacement",
                            configmenu::windowPlacementValue(WP::Cascade)));
  CHECK(Config::load(p).windowPlacement == WP::Cascade);
  std::remove(p);
}
