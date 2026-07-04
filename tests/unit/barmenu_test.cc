// Pure Toolbar/Slit menu builders + the classic rc spellings they persist. No
// onTop row anywhere (program law - no layer machinery, no lying toggle). The
// knobs are configmenu's ConfigOption idiom (append-only values), not per-knob
// Acts. Radios carry target state; toggles carry the knob to flip.
#include <doctest/doctest.h>
#include "Barmenu.hh"
#include "BarSpelling.hh"
#include "Text.hh"

using namespace bbai;

TEST_CASE("toolbar menu: enable + placement(6) + autohide, no onTop") {
  ToolbarConfig tc;
  tc.enabled = true;
  tc.placement = toolbar::Placement::BottomCenter;
  tc.autoHide = false;
  auto m = barmenu::buildToolbar(tc);
  REQUIRE(m.size() == 4);
  CHECK(m[0].action == MenuItem::Act::ConfigOption);
  CHECK(m[0].option == ConfigOption::ToolbarEnabled);
  CHECK(m[0].checked);                              // enabled
  CHECK(m[1].separator());
  CHECK(m[2].kind == MenuItem::Kind::Submenu);
  CHECK(m[2].label == bt::decodeUtf8("Placement"));
  REQUIRE(m[2].submenu_items.size() == 6);
  CHECK(m[2].submenu_items[4].option == ConfigOption::ToolbarPlaceBottomCenter);
  CHECK(m[2].submenu_items[4].checked);            // current placement
  CHECK_FALSE(m[2].submenu_items[0].checked);
  CHECK(m[3].option == ConfigOption::ToolbarAutoHide);
  CHECK_FALSE(m[3].checked);
  for (const MenuItem &it : m)
    CHECK(it.label != bt::decodeUtf8("Always on Top"));   // omitted, program law
}

TEST_CASE("slit menu: direction(2) + placement(8) + autohide, no onTop") {
  SlitConfig sc;
  sc.direction = SlitDirection::Vertical;
  sc.placement = SlitPlacement::CenterRight;
  sc.autoHide = true;
  auto m = barmenu::buildSlit(sc);
  REQUIRE(m.size() == 3);
  CHECK(m[0].kind == MenuItem::Kind::Submenu);
  CHECK(m[0].label == bt::decodeUtf8("Direction"));
  REQUIRE(m[0].submenu_items.size() == 2);
  CHECK(m[0].submenu_items[1].option == ConfigOption::SlitDirVertical);
  CHECK(m[0].submenu_items[1].checked);
  CHECK(m[1].label == bt::decodeUtf8("Placement"));
  REQUIRE(m[1].submenu_items.size() == 8);
  CHECK(m[1].submenu_items[6].option == ConfigOption::SlitPlaceCenterRight);
  CHECK(m[1].submenu_items[6].checked);
  CHECK(m[2].option == ConfigOption::SlitAutoHide);
  CHECK(m[2].checked);
}

TEST_CASE("bar spellings: classic rc value strings") {
  CHECK(std::string(barmenu::toolbarPlacementValue(toolbar::Placement::TopLeft)) == "TopLeft");
  CHECK(std::string(barmenu::toolbarPlacementValue(toolbar::Placement::BottomCenter)) == "BottomCenter");
  CHECK(std::string(barmenu::slitPlacementValue(SlitPlacement::CenterRight)) == "CenterRight");
  CHECK(std::string(barmenu::slitPlacementValue(SlitPlacement::BottomLeft)) == "BottomLeft");
  CHECK(std::string(barmenu::slitDirectionValue(SlitDirection::Horizontal)) == "Horizontal");
  CHECK(std::string(barmenu::slitDirectionValue(SlitDirection::Vertical)) == "Vertical");
}
