// Pure tree fixup: parsed menu-file items -> the live root-menu tree. No
// wlroots, no Menu rendering - buildFromParsed only rewrites MenuItems.
#include <doctest/doctest.h>
#include "Rootmenu.hh"
#include "Workspace.hh"
#include "Text.hh"

using namespace bbai;

namespace {
  std::u32string u(const char *s) { return bt::decodeUtf8(s); }

  MenuItem marked(MenuItem::Act act, const char *label) {
    MenuItem m;
    m.kind = MenuItem::Kind::Submenu;
    m.action = act;
    m.label = bt::decodeUtf8(label);
    return m;
  }
}

TEST_CASE("a WorkspacesMenu placeholder is filled from the live model") {
  WorkspaceModel ws;
  std::vector<MenuItem> parsed;
  parsed.push_back(marked(MenuItem::Act::WorkspacesMenu, "Workspace List"));

  auto items = rootmenu::buildFromParsed(parsed, ws);
  REQUIRE(items.size() == 1);
  CHECK(items[0].label == u("Workspace List"));
  REQUIRE(items[0].submenu_items.size() == ws.count() + 3);  // rows + sep + New + Remove
  CHECK(items[0].submenu_items[ws.current()].checked);
}

TEST_CASE("placeholders are filled recursively inside submenus") {
  WorkspaceModel ws;
  MenuItem outer;
  outer.kind = MenuItem::Kind::Submenu;
  outer.label = u("Outer");
  outer.submenu_items.push_back(marked(MenuItem::Act::WorkspacesMenu, "WS"));
  std::vector<MenuItem> parsed{outer};

  auto items = rootmenu::buildFromParsed(parsed, ws);
  REQUIRE(items.size() == 1);
  REQUIRE(items[0].submenu_items.size() == 1);
  CHECK(items[0].submenu_items[0].submenu_items.size() == ws.count() + 3);
}

TEST_CASE("a ConfigMenu placeholder is disabled until wave-2 mounts it") {
  WorkspaceModel ws;
  std::vector<MenuItem> parsed{marked(MenuItem::Act::ConfigMenu, "Configuration")};

  auto items = rootmenu::buildFromParsed(parsed, ws);
  REQUIRE(items.size() == 1);
  CHECK_FALSE(items[0].enabled);
  CHECK(items[0].submenu_items.empty());
  CHECK_FALSE(items[0].selectable());
}

TEST_CASE("an empty parse falls back to the in-code menu, not classic's stub") {
  WorkspaceModel ws;
  auto items = rootmenu::buildFromParsed({}, ws);
  auto incode = rootmenu::build(ws);
  REQUIRE(items.size() == incode.size());
  CHECK(items[0].label == incode[0].label);   // kitty - the richer default (locked)
}

TEST_CASE("a tree without placeholders passes through untouched") {
  WorkspaceModel ws;
  MenuItem e;
  e.label = u("xterm");
  e.action = MenuItem::Act::Exec;
  e.argv = {"xterm"};

  auto items = rootmenu::buildFromParsed({e}, ws);
  REQUIRE(items.size() == 1);
  CHECK(items[0].argv == std::vector<std::string>{"xterm"});
}
