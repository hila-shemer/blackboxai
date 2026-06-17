#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "DecorationPalette.hh"
using namespace bbai::deco;
TEST_CASE("active and inactive looks differ but active == M3 constants") {
  CHECK(std::string(lookFor(Element::Title, true).c1)  == "#c0c0c0");
  CHECK(std::string(lookFor(Element::Label, true).c1)  == "#b8b8b8");
  CHECK(std::string(lookFor(Element::Title, false).c1) != std::string(lookFor(Element::Title, true).c1));
  CHECK(std::string(lookFor(Element::Label, false).c1) != std::string(lookFor(Element::Label, true).c1));
  CHECK(textColorFor(false).red()   != textColorFor(true).red());
  CHECK(borderColorFor(false).red() != borderColorFor(true).red());
}
