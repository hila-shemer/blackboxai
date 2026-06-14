// Coverage + behavior for the full bt::Image renderer: every gradient kind
// (plain and interlaced branches), both bevels, and the interlace helper.
#include <doctest/doctest.h>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include "Image.hh"
#include "Texture.hh"

using bt::Image;
using bt::Texture;
using bt::Color;

static uint32_t argb(uint8_t r, uint8_t g, uint8_t b) {
    return (0xFFu << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

TEST_CASE("every gradient kind fills a varied, fully-opaque buffer") {
    const char *kinds[] = {
        "diagonal", "crossdiagonal", "rectangle", "pyramid", "pipecross",
        "elliptic", "horizontal", "vertical", "splitvertical",
    };
    for (const char *k : kinds) {
        CAPTURE(k);
        for (bool interlaced : {false, true}) {
            CAPTURE(interlaced);
            Texture t;
            std::string d = std::string("flat gradient ") + k;
            if (interlaced) d += " interlaced";
            t.setDescription(d);
            t.setColor1(Color(0, 0, 0));
            t.setColor2(Color(255, 255, 255));

            Image img(24, 18);
            std::vector<uint32_t> buf = img.renderBuffer(t);
            REQUIRE(buf.size() == 24u * 18u);

            uint32_t lo = 0xFFFFFFFFu, hi = 0u;
            for (uint32_t px : buf) {
                CHECK((px >> 24) == 0xFFu);          // opaque
                uint32_t rgb = px & 0x00FFFFFFu;
                lo = std::min(lo, rgb);
                hi = std::max(hi, rgb);
            }
            CHECK(lo != hi);                          // the gradient actually varies
        }
    }
}

TEST_CASE("diagonal gradient endpoints survive the kind sweep (exact anchor)") {
    Texture t;
    t.setDescription("flat gradient diagonal");
    t.setColor1(Color(0, 0, 0));
    t.setColor2(Color(255, 255, 255));
    Image img(16, 16);
    std::vector<uint32_t> buf = img.renderBuffer(t);
    CHECK(buf[0] == argb(0, 0, 0));
    CHECK(buf[16 * 16 - 1] == argb(238, 238, 238));
}

TEST_CASE("raised bevel lightens the top edge; sunken differs") {
    auto render = [](const char *d) {
        Texture t;
        t.setDescription(d);
        t.setColor1(Color(100, 100, 100));
        Image img(8, 8);
        return img.renderBuffer(t);
    };
    std::vector<uint32_t> raised = render("raised solid");
    std::vector<uint32_t> sunken = render("sunken solid");

    // raisedBevel brightens the top row to the light color: 100 + (100>>1) = 150.
    CHECK(raised[0] == argb(150, 150, 150));
    // the two bevels produce different top-left pixels...
    CHECK(raised[0] != sunken[0]);
    // ...and both differ from the flat fill somewhere (the bevel ran).
    bool raised_varied = false, sunken_varied = false;
    for (uint32_t p : raised) if (p != argb(100, 100, 100)) raised_varied = true;
    for (uint32_t p : sunken) if (p != argb(100, 100, 100)) sunken_varied = true;
    CHECK(raised_varied);
    CHECK(sunken_varied);
}

TEST_CASE("interlaced solid darkens alternate rows to 7/8") {
    Texture t;
    t.setDescription("flat solid interlaced");
    t.setColor1(Color(200, 200, 200));
    Image img(4, 4);
    std::vector<uint32_t> buf = img.renderBuffer(t);
    CHECK(buf[0 * 4 + 0] == argb(200, 200, 200));  // even row unchanged
    CHECK(buf[1 * 4 + 0] == argb(175, 175, 175));  // odd row: 200*7/8 = 175
    CHECK(buf[2 * 4 + 0] == argb(200, 200, 200));
    CHECK(buf[3 * 4 + 0] == argb(175, 175, 175));
}
