// Unit test for the pure frame-geometry math (src/Frame.hh) — the single source
// of truth shared by the decoration renderer and the input hit-test. Pins the
// worked example from the M3 plan so an off-by-one in the layout is caught
// without booting a compositor.
#include <doctest/doctest.h>
#include "Frame.hh"

using namespace bbai::frame;

TEST_CASE("frame metrics match the pinned M3 defaults") {
    CHECK(kBorder == 1);
    CHECK(kTitleHeight == 23);
    CHECK(kHandleHeight == 6);
    CHECK(kGripWidth == 38);
    CHECK(kButtonWidth == 19);
    CHECK(kLabelHeight == 19);
    CHECK(kButtonStep == 21);
}

TEST_CASE("frame size + element layout for 640x480 content (plan worked example)") {
    const int W = 640, H = 480;
    CHECK(frameWidth(W) == 642);
    CHECK(frameHeight(H) == 509);
    CHECK(clientX() == 1);
    CHECK(clientY() == 23);

    CHECK(title(W, H).x == 0);
    CHECK(title(W, H).w == 642);
    CHECK(title(W, H).h == 23);

    CHECK(iconifyButton(W, H).x == 2);
    CHECK(iconifyButton(W, H).y == 2);
    CHECK(closeButton(W, H).x == 621);     // frameWidth - 21
    CHECK(maximizeButton(W, H).x == 600);  // frameWidth - 42

    const Rect lbl = label(W, H);
    CHECK(lbl.x == 23);
    CHECK(lbl.w == 575);   // 642 - 67
    CHECK(lbl.h == 19);

    CHECK(handle(W, H).y == 503);          // titleHeight + H
    CHECK(handle(W, H).h == 6);
    CHECK(leftGrip(W, H).x == 0);
    CHECK(leftGrip(W, H).w == 38);
    CHECK(rightGrip(W, H).x == 604);       // frameWidth - 38

    CHECK(leftBorder(W, H).x == 0);
    CHECK(leftBorder(W, H).w == 1);
    CHECK(leftBorder(W, H).h == 480);
    CHECK(rightBorder(W, H).x == 641);     // frameWidth - 1
}

TEST_CASE("small content still yields a positive label width") {
    // 200x150 (the test client): label must stay >= 1 between the buttons.
    CHECK(frameWidth(200) == 202);
    CHECK(frameHeight(150) == 179);
    CHECK(label(200, 150).w == 135);       // 202 - 67
    CHECK(label(200, 150).w >= 1);
}
