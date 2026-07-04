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

TEST_CASE("X11 grey ramp - exact rgb.txt values, not a rounding formula") {
    using bt::Color;
    CHECK(Color::fromString("grey10") == Color(26, 26, 26));
    CHECK(Color::fromString("grey20") == Color(51, 51, 51));
    CHECK(Color::fromString("grey30") == Color(77, 77, 77));     // 76.5 rounds UP upstream
    CHECK(Color::fromString("gray40") == Color(102, 102, 102));
    CHECK(Color::fromString("grey50") == Color(127, 127, 127));  // 127.5 rounds DOWN upstream
    CHECK(Color::fromString("grey60") == Color(153, 153, 153));
    CHECK(Color::fromString("grey68") == Color(173, 173, 173));
    CHECK(Color::fromString("grey70") == Color(179, 179, 179));
    CHECK(Color::fromString("grey72") == Color(184, 184, 184));
    CHECK(Color::fromString("grey74") == Color(189, 189, 189));
    CHECK(Color::fromString("grey77") == Color(196, 196, 196));
    CHECK(Color::fromString("grey80") == Color(204, 204, 204));
    CHECK(Color::fromString("grey85") == Color(217, 217, 217));
    CHECK(Color::fromString("gray90") == Color(229, 229, 229));
    CHECK(Color::fromString("Grey85") == Color(217, 217, 217));  // case-insensitive
    CHECK_FALSE(Color::fromString("grey101").valid());
    CHECK_FALSE(Color::fromString("greyish").valid());
}

TEST_CASE("named colors the shipped styles use") {
    using bt::Color;
    CHECK(Color::fromString("MidnightBlue") == Color(25, 25, 112));
    CHECK(Color::fromString("SteelBlue")    == Color(70, 130, 180));
    CHECK(Color::fromString("SlateGrey")    == Color(112, 128, 144));  // Minimal's rootCommand
    CHECK(Color::fromString("slategray")    == Color(112, 128, 144));
    CHECK(Color::fromString("darkgray")     == Color(169, 169, 169));
    CHECK(Color::fromString("Grey")         == Color(190, 190, 190));
}
