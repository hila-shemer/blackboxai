// Pure Windowmenu build - the classic item set minus the accepted parity gaps
// (no Shade: xdg-shell has none; no onTop: program law, no layer machinery;
// no OccupyAll/vert-horz/kill: no state/policy for them). What ships:
// SendToWorkspace submenu (current disabled+checked), sep, Iconify, Maximize
// (checked when maximized), sep, Close. enabled/checked computed at build time
// (menus rebuild per open, so the marks are always fresh).
#include <doctest/doctest.h>
#include "Windowmenu.hh"
#include "Text.hh"

using namespace bbai;

namespace {
  // A tiny View stand-in is impossible (View needs a Server + toplevel), so the
  // builder takes the two facts it needs behind a narrow struct in the header.
  windowmenu::ViewFacts facts(unsigned ws, bool maxed, void *id) {
    return { ws, maxed, id };
  }
}

TEST_CASE("shape: send-to submenu, Iconify, Maximize, Close - no Shade/onTop") {
  WorkspaceModel ws(3);
  int handle = 0;
  auto items = windowmenu::buildFrom(facts(1, false, &handle), ws);
  REQUIRE(items.size() == 6);
  CHECK(items[0].kind == MenuItem::Kind::Submenu);
  CHECK(items[0].label == bt::decodeUtf8("Send To..."));
  REQUIRE(items[0].submenu_items.size() == 3);
  CHECK(items[1].separator());
  CHECK(items[2].action == MenuItem::Act::Iconify);
  CHECK(items[2].label == bt::decodeUtf8("Iconify"));
  CHECK(items[2].target == &handle);
  CHECK(items[3].action == MenuItem::Act::MaximizeToggle);
  CHECK_FALSE(items[3].checked);
  CHECK(items[4].separator());
  CHECK(items[5].action == MenuItem::Act::Close);
  CHECK(items[5].target == &handle);
}

TEST_CASE("send-to rows: current workspace is disabled + checked, others live") {
  WorkspaceModel ws(3);
  int handle = 0;
  auto sub = windowmenu::buildFrom(facts(1, false, &handle), ws)[0].submenu_items;
  CHECK(sub[0].action == MenuItem::Act::SendToWorkspace);
  CHECK(sub[0].workspace == 0u);
  CHECK(sub[0].enabled);
  CHECK_FALSE(sub[0].checked);
  CHECK_FALSE(sub[1].enabled);      // current (ws 1): disabled
  CHECK(sub[1].checked);
  CHECK(sub[1].workspace == 1u);
  CHECK(sub[2].enabled);
  CHECK(sub[2].label == bt::decodeUtf8(ws.name(2).c_str()));
}

TEST_CASE("Maximize is checked when the window is maximized") {
  WorkspaceModel ws(2);
  int handle = 0;
  CHECK(windowmenu::buildFrom(facts(0, true, &handle), ws)[3].checked);
}
