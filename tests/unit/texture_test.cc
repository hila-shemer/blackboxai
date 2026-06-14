#include <doctest/doctest.h>
#include "Texture.hh"

using bt::Texture;
using bt::Color;

TEST_CASE("setDescription parses appearance flags") {
    Texture t;
    t.setDescription("raised gradient diagonal");
    CHECK((t.texture() & Texture::Raised));
    CHECK((t.texture() & Texture::Gradient));
    CHECK((t.texture() & Texture::Diagonal));
    CHECK_FALSE((t.texture() & Texture::Sunken));
    CHECK_FALSE((t.texture() & Texture::Solid));
}

TEST_CASE("setDescription parses solid + flat + interlaced + border") {
    Texture t;
    t.setDescription("flat solid interlaced border");
    CHECK((t.texture() & Texture::Flat));
    CHECK((t.texture() & Texture::Solid));
    CHECK((t.texture() & Texture::Interlaced));
    CHECK((t.texture() & Texture::Border));
    CHECK_FALSE((t.texture() & Texture::Gradient));
}

TEST_CASE("setDescription defaults: no bevel word -> raised; gradient with no kind -> diagonal") {
    Texture t;
    t.setDescription("gradient");
    CHECK((t.texture() & Texture::Gradient));
    CHECK((t.texture() & Texture::Diagonal));
    CHECK((t.texture() & Texture::Raised));
}

TEST_CASE("parentrelative wins exclusively") {
    Texture t;
    t.setDescription("parentrelative");
    CHECK((t.texture() & Texture::Parent_Relative));
    CHECK_FALSE((t.texture() & Texture::Solid));
    CHECK_FALSE((t.texture() & Texture::Gradient));
}

TEST_CASE("colors are independently settable") {
    Texture t;
    t.setColor1(Color(10, 20, 30));
    t.setColor2(Color(40, 50, 60));
    t.setBorderColor(Color(1, 2, 3));
    CHECK(t.color1() == Color(10, 20, 30));
    CHECK(t.color2() == Color(40, 50, 60));
    CHECK(t.borderColor() == Color(1, 2, 3));
}

TEST_CASE("setColor1 derives light and shadow colors (verbatim Blackbox math)") {
    Texture t;
    t.setColor1(Color(100, 100, 100));
    // light  = c + (c >> 1)            = 100 + 50 = 150
    // shadow = (c >> 2) + (c >> 1)     = 25 + 50  = 75
    CHECK(t.lightColor()  == Color(150, 150, 150));
    CHECK(t.shadowColor() == Color(75, 75, 75));
}

TEST_CASE("setColor1 light channel clamps on overflow") {
    Texture t;
    t.setColor1(Color(200, 0, 255));
    // red:  200 + 100 = 300 -> wraps in uchar (44) < 200 -> clamped to 255
    // blue: 255 + 127 = 382 -> wraps (126)  < 255 -> clamped to 255
    CHECK(t.lightColor().red()  == 255);
    CHECK(t.lightColor().blue() == 255);
}
