#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"

TEST_CASE("headless background matches the golden Blackbox gradient") {
    bbai::test::Frame frame = bbai::test::captureFirstFrame();
    CHECK(frame.w == 1280u);
    CHECK(frame.h == 720u);
    CHECK(bbai::test::compareGolden(
        frame, "tests/golden/m1-background-diagonal-gradient.png",
        /*tolerance=*/2, /*pixel_budget=*/0));
}
