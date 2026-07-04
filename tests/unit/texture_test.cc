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

#include "Resource.hh"

TEST_CASE("textureResource: Results-style exact keys (0.65 syntax, color/colorTo)") {
    bt::Resource r;
    r.loadFromString(
        "menu.title:                             raised diagonal interlaced gradient bevel1 border border\n"
        "     menu.title.color:                  grey20\n"
        "     menu.title.colorTo:                rgb:8/8/7\n"
        "menu.title.borderWidth:                 1\n"
        "menu.title.borderColor:                 rgb:2/2/1c\n");
    bt::Texture t = bt::textureResource(r, "menu.title", "Menu.Title", "black");
    CHECK((t.texture() & bt::Texture::Gradient));
    CHECK((t.texture() & bt::Texture::Diagonal));
    CHECK((t.texture() & bt::Texture::Interlaced));
    CHECK((t.texture() & bt::Texture::Border));
    CHECK((t.texture() & bt::Texture::Raised));
    CHECK(t.color1() == bt::Color(51, 51, 51));            // grey20
    CHECK(t.color2() == bt::Color(0x88, 0x88, 0x77));      // rgb:8/8/7 (nibble doubling)
    CHECK(t.borderColor() == bt::Color(0x22, 0x22, 0x1c));
    CHECK(t.borderWidth() == 1u);
}

TEST_CASE("textureResource: Gray-style .appearance + wildcard backgroundColor") {
    bt::Resource r;
    r.loadFromString(
        "*backgroundColor: rgb:dd/dd/d7\n"
        "*button.pressed.appearance: sunken solid\n"
        "*button.pressed.backgroundColor: darkgrey\n"
        "window.handle.focus.appearance: raised solid border\n"
        "*borderColor: black\n"
        "*borderWidth: 1\n");
    // .appearance chain + solid backgroundColor chain (via wildcards)
    bt::Texture h = bt::textureResource(r, "window.handle.focus", "Window.Handle.Focus", "white");
    CHECK((h.texture() & bt::Texture::Solid));
    CHECK((h.texture() & bt::Texture::Border));
    CHECK(h.color1() == bt::Color(0xdd, 0xdd, 0xd7));
    CHECK(h.borderColor() == bt::Color(0, 0, 0));
    bt::Texture p = bt::textureResource(r, "toolbar.button.pressed", "Toolbar.Button.Pressed", "black");
    CHECK((p.texture() & bt::Texture::Sunken));
    CHECK(p.color1() == bt::Color(169, 169, 169));         // darkgrey
}

TEST_CASE("textureResource: missing description falls to flat-solid defaultColor") {
    bt::Resource r;
    r.loadFromString("unrelated.key: 1\n");
    bt::Texture t = bt::textureResource(r, "window.title.focus", "Window.Title.Focus", "white");
    CHECK((t.texture() & bt::Texture::Solid));
    CHECK((t.texture() & bt::Texture::Flat));
    CHECK(t.color1() == bt::Color(255, 255, 255));
}

TEST_CASE("textureResource: defaultTexture overload (the slit chain)") {
    bt::Resource r;
    r.loadFromString("toolbar: flat solid\ntoolbar.backgroundColor: grey20\n");
    bt::Texture tb = bt::textureResource(r, "toolbar", "Toolbar", "white");
    bt::Texture s = bt::textureResource(r, "slit", "Slit", tb);   // no slit keys -> toolbar's
    CHECK(s == tb);
}

TEST_CASE("textureResource: an unparseable color sanitizes to black, not the invalid sentinel") {
    bt::Resource r;
    r.loadFromString("x: flat solid\nx.backgroundColor: chartreuse-ish\n");
    bt::Texture t = bt::textureResource(r, "x", "X", "white");
    CHECK(t.color1() == bt::Color(0, 0, 0));
}
