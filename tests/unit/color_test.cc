#include <doctest/doctest.h>
#include "Color.hh"

using bt::Color;

TEST_CASE("parse #rrggbb") {
    Color c = Color::fromString("#ff8000");
    CHECK(c.valid());
    CHECK(c.red() == 0xff);
    CHECK(c.green() == 0x80);
    CHECK(c.blue() == 0x00);
}

TEST_CASE("parse short #rgb expands by nibble replication") {
    Color c = Color::fromString("#f80");
    CHECK(c.red() == 0xff);
    CHECK(c.green() == 0x88);
    CHECK(c.blue() == 0x00);
}

TEST_CASE("parse rgb:rr/gg/bb form") {
    Color c = Color::fromString("rgb:20/40/60");
    CHECK(c.red() == 0x20);
    CHECK(c.green() == 0x40);
    CHECK(c.blue() == 0x60);
}

TEST_CASE("parse named colors") {
    CHECK(Color::fromString("black")  == Color(0, 0, 0));
    CHECK(Color::fromString("white")  == Color(255, 255, 255));
    CHECK(Color::fromString("grey20") == Color(51, 51, 51));
    CHECK(Color::fromString("WHITE")  == Color(255, 255, 255)); // case-insensitive
}

TEST_CASE("invalid color is invalid") {
    CHECK_FALSE(Color::fromString("not-a-color").valid());
    CHECK_FALSE(Color::fromString("#12").valid());   // wrong length
    CHECK_FALSE(Color::fromString("#gg0000").valid()); // non-hex
    CHECK_FALSE(Color().valid());
}
