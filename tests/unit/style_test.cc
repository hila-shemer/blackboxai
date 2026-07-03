// tests/unit/style_test.cc
// Style loader policy tests. Runs under the isolated fontconfig (own
// executable + FONTCONFIG_FILE + workdir=source root) because Style
// constructs TextRenderers, and the metric formulas consume real font heights.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "Style.hh"
#include "Text.hh"

#include <algorithm>

using namespace bbai;

TEST_CASE("Results loads: 0.65 exact-key syntax, textures, colors, rootCommand") {
  auto s = Style::load("data/styles/Results");
  REQUIRE(s);
  CHECK(s->sourcePath() == "data/styles/Results");

  const WindowLook &f = s->windowLook(true);
  CHECK((f.title.texture() & bt::Texture::Gradient));
  CHECK((f.title.texture() & bt::Texture::Diagonal));
  CHECK((f.title.texture() & bt::Texture::Raised));
  CHECK(f.title.color1() == bt::Color::fromString("rgb:8/8/7"));
  CHECK(f.title.color2() == bt::Color::fromString("grey20"));
  CHECK((f.label.texture() & bt::Texture::Interlaced));
  CHECK((f.label.texture() & bt::Texture::Sunken));
  CHECK(f.text == bt::Color(255, 255, 255));                 // window.label.focus.textColor white
  CHECK(f.foreground == bt::Color(0, 0, 0));                 // picColor black

  const WindowLook &u = s->windowLook(false);
  CHECK(u.text == bt::Color::fromString("grey"));
  CHECK(u.foreground == bt::Color::fromString("grey40"));

  // menu: borders + fonts + colors
  const MenuLook &m = s->menuLook();
  CHECK((m.title.texture() & bt::Texture::Border));
  CHECK(m.title.borderWidth() == 1u);
  CHECK(m.title.borderColor() == bt::Color::fromString("rgb:2/2/1c"));
  CHECK(m.titleText == bt::Color::fromString("grey85"));
  CHECK(m.frameDisabled == bt::Color::fromString("rgb:4/4/38"));

  // toolbar textures
  const ToolbarLook &t = s->toolbarLook();
  CHECK((t.bar.texture() & bt::Texture::Gradient));
  CHECK(t.slabelText == bt::Color::fromString("grey85"));

  // slit falls back to the toolbar texture when no slit keys exist... Results
  // has none, so slitTexture == toolbar bar texture.
  CHECK(s->slitTexture() == t.bar);

  // rootCommand is carried verbatim (and NEVER exec'd - Server policy).
  CHECK(s->rootCommand() == "bsetroot -mod 4 4 -fg rgb:6/6/5c -bg grey20");

  // metrics: handleHeight key is 4, handle textures carry no border -> 4.
  CHECK(s->frameMetrics().handleHeight == 4);
  CHECK(s->frameMetrics().border == 1);   // window.frame.borderWidth absent -> reference default 1
  CHECK(s->borderWidth() == 1);
  CHECK(s->bevelWidth() == 2);
}

TEST_CASE("Gray loads: wildcards resolve, PR sanity rules, PR labels honored") {
  auto s = Style::load("data/styles/Gray");
  REQUIRE(s);

  // toolbar*textColor: black - through the wildcard matcher
  CHECK(s->toolbarLook().clockText == bt::Color(0, 0, 0));
  CHECK(s->toolbarLook().wlabelText == bt::Color(0, 0, 0));

  // PR label/button are HONORED (drawn by parent pixel-copy at emit time)...
  CHECK(s->windowLook(true).label.texture() == bt::Texture::Parent_Relative);
  CHECK(s->windowLook(true).button.texture() == bt::Texture::Parent_Relative);
  CHECK(s->toolbarLook().slabel.texture() == bt::Texture::Parent_Relative);

  // ...while PR on a CONTAINER would be force-replaced (Gray's containers are
  // not PR; the synthetic case below pins the rule).
  CHECK((s->windowLook(true).title.texture() & bt::Texture::Gradient));

  // window.handleHeight: 8, PLUS the classic border fold: Gray's handle
  // appearance carries 'border' and *borderWidth: 1 reaches it through the
  // wildcard, and ScreenResource.cc:405 adds maxBw(handle)*2 -> 8 + 2 = 10.
  // (The plan expected 8; the verbatim classic derivation says 10.)
  CHECK(s->frameMetrics().handleHeight == 10);

  // *font resolves the toolbar/menu-frame fonts
  REQUIRE(s->toolbarFont() != nullptr);
  CHECK(s->toolbarFont()->ok());
  CHECK(s->menuFrameFont()->ok());
}

TEST_CASE("PR sanity: parentrelative containers force flat black") {
  bt::Resource r;
  r.loadFromString(
    "window.title.focus: parentrelative\n"
    "window.grip.unfocus: parentrelative\n"
    "toolbar: parentrelative\n"
    "slit: parentrelative\n"
    "menu.frame: parentrelative\n");
  auto s = Style::fromResource(r);
  REQUIRE(s);
  bt::Texture flat_black;
  flat_black.setDescription("flat solid");
  flat_black.setColor1(bt::Color(0, 0, 0));
  CHECK(s->windowLook(true).title == flat_black);
  CHECK(s->windowLook(false).grip == flat_black);
  CHECK(s->toolbarLook().bar == flat_black);
  CHECK(s->slitTexture() == flat_black);
  // menu containers too (deviation from classic, which never sanitized menu
  // textures because X PR meant "root shows through" - we have no root window;
  // none of the 19 shipped styles uses PR menus).
  CHECK(s->menuLook().frame == flat_black);
}

TEST_CASE("metric formulas match the classic derivation (computed from the live font)") {
  bt::Resource r;
  r.loadFromString(
    "window.font: monospace:pixelsize=16\n"
    "toolbar.font: monospace:pixelsize=16\n"
    "window.title.marginWidth: 2\n"
    "window.label.marginWidth: 2\n"
    "window.button.marginWidth: 2\n"
    "window.handleHeight: 6\n"
    "toolbar.marginWidth: 2\n"
    "toolbar.label.marginWidth: 2\n"
    "toolbar.button.marginWidth: 2\n");
  auto s = Style::fromResource(r);
  REQUIRE(s);
  const int th = s->windowFont()->height();   // bundled font under isolation
  const frame::FrameMetrics &fm = s->frameMetrics();
  // classic: button = 9(bitmap) + 2*(bw0+margin2) = 13; label = max(th+2*(0+2), 13);
  // button = max(13, label); title = label + 2*(0+2); grip = 2*button; handle = 6+0.
  const int label = std::max(th + 4, 13);
  CHECK(fm.labelHeight == label);
  CHECK(fm.buttonWidth == std::max(13, label));
  CHECK(fm.titleHeight == label + 4);
  CHECK(fm.gripWidth == 2 * fm.buttonWidth);
  CHECK(fm.handleHeight == 6);
  CHECK(fm.titleMargin == 2);   // borderWidth 0 + marginWidth 2
  const toolbar::ToolbarMetrics &tm = s->toolbarMetrics();
  CHECK(tm.labelHeight == std::max(th + 4, 13));
  CHECK(tm.barHeight == tm.labelHeight + 4);
  CHECK(tm.hiddenHeight == 2);   // max(bw0 + frameMargin2, 1)
}

TEST_CASE("builtin: today's look, pinned M3/M4 metrics") {
  auto s = Style::builtin();
  REQUIRE(s);
  CHECK(s->sourcePath().empty());
  // Pinned metrics - the whole existing golden suite keys off these.
  CHECK(s->frameMetrics().titleHeight == 23);
  CHECK(s->frameMetrics().labelHeight == 19);
  CHECK(s->frameMetrics().buttonWidth == 19);
  CHECK(s->frameMetrics().handleHeight == 6);
  CHECK(s->frameMetrics().gripWidth == 38);
  CHECK(s->toolbarMetrics().barHeight == 23);
  CHECK(s->toolbarMetrics().labelHeight == 19);
  // The M3 palette, encoded as a style string.
  CHECK(s->windowLook(true).title.color1() == bt::Color::fromString("#c0c0c0"));
  CHECK(s->windowLook(false).label.color2() == bt::Color::fromString("#686868"));
  CHECK(s->windowLook(true).frameBorder == bt::Color(48, 48, 48));
  CHECK(s->windowLook(false).text == bt::Color(96, 96, 96));
  CHECK(s->toolbarLook().bar.color1() == bt::Color::fromString("#c0c0c0"));
  CHECK(s->menuLook().active.color1() == bt::Color::fromString("#5a7abf"));
  CHECK(s->menuLook().frameDisabled == bt::Color(112, 112, 112));
  // The desktop texture is the current hardcoded default.
  CHECK(s->desktop().texture.color1() == bt::Color::fromString("#204060"));
  REQUIRE(s->windowFont() != nullptr);
  CHECK(s->windowFont()->ok());
}

TEST_CASE("load: unreadable path returns nullptr (the ladder lives in Server)") {
  CHECK(Style::load("/nonexistent/style") == nullptr);
}
