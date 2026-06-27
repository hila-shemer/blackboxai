// Unit tests for the Blackbox menu-file parser (src/MenuParser). Pure: text in,
// MenuItem tree out. No wlroots, no live menu. The real reference menu.in lives
// under tests/fixtures/ and is reached via the BBAI_MENU_FIXTURE compile define.
#include <doctest/doctest.h>
#include "MenuParser.hh"
#include "Text.hh"

#include <string>

using namespace bbai;
using menuparser::Result;

namespace {
  // UTF-8 -> u32 so test literals stay readable.
  std::u32string u(const char *s) { return bt::decodeUtf8(s); }

  // The Exec mapping shells out via /bin/sh -c <command>.
  std::vector<std::string> sh(const std::string &cmd) {
    return {"/bin/sh", "-c", cmd};
  }
}

TEST_CASE("flat menu: begin title + exec/exit items map to the right actions") {
  Result r = menuparser::parse(
    "[begin] (My Menu)\n"
    "  [exec] (xterm) {xterm -ls}\n"
    "  [exec] (Editor) {vi}\n"
    "  [exit] (Quit)\n"
    "[end]\n");

  CHECK(r.title == u("My Menu"));
  REQUIRE(r.items.size() == 3);

  CHECK(r.items[0].kind == MenuItem::Kind::Command);
  CHECK(r.items[0].action == MenuItem::Act::Exec);
  CHECK(r.items[0].label == u("xterm"));
  CHECK(r.items[0].argv == sh("xterm -ls"));

  CHECK(r.items[1].action == MenuItem::Act::Exec);
  CHECK(r.items[1].label == u("Editor"));
  CHECK(r.items[1].argv == sh("vi"));

  CHECK(r.items[2].kind == MenuItem::Kind::Command);
  CHECK(r.items[2].action == MenuItem::Act::Exit);
  CHECK(r.items[2].label == u("Quit"));
}

TEST_CASE("separator and nop") {
  Result r = menuparser::parse(
    "[begin] (m)\n"
    "  [exec] (a) {a}\n"
    "  [sep]\n"
    "  [nop] (a label)\n"
    "  [nop]\n"
    "[end]\n");

  REQUIRE(r.items.size() == 4);
  CHECK(r.items[1].kind == MenuItem::Kind::Separator);
  CHECK(r.items[1].separator());

  // [nop] is an inert (disabled) command; label optional.
  CHECK(r.items[2].kind == MenuItem::Kind::Command);
  CHECK(r.items[2].action == MenuItem::Act::None);
  CHECK_FALSE(r.items[2].enabled);
  CHECK(r.items[2].label == u("a label"));
  CHECK_FALSE(r.items[2].selectable());

  CHECK_FALSE(r.items[3].enabled);
  CHECK(r.items[3].label.empty());
}

TEST_CASE("nested submenu builds the right tree, closed by [end]") {
  Result r = menuparser::parse(
    "[begin] (root)\n"
    "  [exec] (top) {top}\n"
    "  [submenu] (Graphics) {Graphics title}\n"
    "    [exec] (gimp) {gimp}\n"
    "    [submenu] (More)\n"
    "      [exec] (deep) {deep}\n"
    "    [end]\n"
    "  [end]\n"
    "  [exit] (Quit)\n"
    "[end]\n");

  REQUIRE(r.items.size() == 3);
  CHECK(r.items[0].label == u("top"));

  const MenuItem &g = r.items[1];
  CHECK(g.kind == MenuItem::Kind::Submenu);
  CHECK(g.label == u("Graphics"));
  REQUIRE(g.submenu_items.size() == 2);
  CHECK(g.submenu_items[0].action == MenuItem::Act::Exec);
  CHECK(g.submenu_items[0].label == u("gimp"));

  const MenuItem &more = g.submenu_items[1];
  CHECK(more.kind == MenuItem::Kind::Submenu);
  CHECK(more.label == u("More"));
  REQUIRE(more.submenu_items.size() == 1);
  CHECK(more.submenu_items[0].label == u("deep"));

  // The [exit] after the submenu's [end] belongs back at the top level.
  CHECK(r.items[2].action == MenuItem::Act::Exit);
}
