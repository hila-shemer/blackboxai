#include <doctest/doctest.h>
#include <vector>
#include <cstdint>
#include "Image.hh"
#include "Texture.hh"

using bt::Image;
using bt::Texture;
using bt::Color;

static uint32_t argb(uint8_t r, uint8_t g, uint8_t b) {
    return (0xFFu << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

TEST_CASE("solid texture fills the whole buffer with color1, fully opaque") {
    Texture t;
    t.setDescription("flat solid");
    t.setColor1(Color(0x20, 0x40, 0x60));
    Image img(4, 3);
    std::vector<uint32_t> buf = img.renderBuffer(t);
    REQUIRE(buf.size() == 4u * 3u);
    for (uint32_t px : buf) CHECK(px == argb(0x20, 0x40, 0x60));
}

TEST_CASE("diagonal gradient reproduces the verbatim Blackbox dgradient values") {
    // The upstream diagonal gradient builds its colour ramp over a *doubled*
    // space (w = width*2, h = height*2): xt[x] = from + (to-from)*x/(2*width).
    // For a 16x16 black->white gradient that gives, per channel:
    //   top-left  (0,0)  : xt[0]  + yt[0]  =   0 +   0 =   0
    //   centre    (8,8)  : xt[8]  + yt[8]  =  63 +  63 = 126
    //   bot-right (15,15): xt[15] + yt[15] = 119 + 119 = 238   (NOT 255!)
    Texture t;
    t.setDescription("flat gradient diagonal");
    t.setColor1(Color(0, 0, 0));
    t.setColor2(Color(255, 255, 255));
    Image img(16, 16);
    std::vector<uint32_t> buf = img.renderBuffer(t);
    REQUIRE(buf.size() == 16u * 16u);

    CHECK(buf[0]            == argb(0, 0, 0));        // top-left == color1 exactly
    CHECK(buf[8 * 16 + 8]   == argb(126, 126, 126));  // centre
    CHECK(buf[16 * 16 - 1]  == argb(238, 238, 238));  // bottom-right (doubled-space)

    for (uint32_t px : buf) CHECK((px >> 24) == 0xFFu); // every pixel opaque
}

TEST_CASE("border draws a frame in borderColor over the fill") {
    Texture t;
    t.setDescription("flat solid border");
    t.setColor1(Color(0, 0, 0));
    t.setBorderColor(Color(255, 0, 0));
    t.setBorderWidth(2);
    Image img(8, 8);
    std::vector<uint32_t> buf = img.renderBuffer(t);
    CHECK(buf[0]         == argb(255, 0, 0)); // outer ring is border
    CHECK(buf[8 * 1 + 1] == argb(255, 0, 0)); // 2nd ring still border (bw=2)
    CHECK(buf[8 * 2 + 2] == argb(0, 0, 0));   // inside is the fill
}
