#include <doctest/doctest.h>
#include "Config.hh"

using bbai::Config;
using bbai::FocusModel;
using bbai::WindowPlacement;
using bbai::ToolbarPlacement;

// Build a Config from an in-memory resource body so cases stay self-contained.
static Config parse(const std::string &body, unsigned screen = 0) {
  bt::Resource r;
  r.loadFromString(body);
  return Config::fromResource(r, screen);
}

TEST_CASE("absent keys take the blackboxwm reference defaults") {
  Config c = parse("");  // nothing set -> every field is the reference default
  CHECK(c.focusModel == FocusModel::ClickToFocus);
  CHECK(c.autoRaise == false);
  CHECK(c.clickRaise == false);
  CHECK(c.focusNewWindows == true);            // reference defaults True
  CHECK(c.autoRaiseDelay == 400);
  CHECK(c.doubleClickInterval == 250);
  CHECK(c.windowPlacement == WindowPlacement::RowSmart);
  CHECK(c.workspaceCount == 4u);
  CHECK(c.workspaceNames.empty());
  CHECK(c.toolbar.enabled == true);
  CHECK(c.toolbar.placement == ToolbarPlacement::BottomCenter);
  CHECK(c.toolbar.alwaysOnTop == false);
  CHECK(c.toolbar.autoHide == false);
  CHECK(c.toolbar.widthPercent == 66);
}

TEST_CASE("present keys parse to their typed values") {
  Config c = parse(
    "session.focusModel: ClickToFocus\n"
    "session.focusNewWindows: False\n"
    "session.autoRaiseDelay: 700\n"
    "session.doubleClickInterval: 300\n"
    "session.windowPlacement: ColSmartPlacement\n"
    "session.screen0.workspaces: 6\n"
    "session.screen0.workspaceNames: one,two,three\n"
    "session.screen0.enableToolbar: False\n"
    "session.screen0.toolbar.placement: TopLeft\n"
    "session.screen0.toolbar.autoHide: True\n"
    "session.screen0.toolbar.onTop: True\n"
    "session.screen0.toolbar.widthPercent: 90\n");
  CHECK(c.focusModel == FocusModel::ClickToFocus);
  CHECK(c.focusNewWindows == false);
  CHECK(c.autoRaiseDelay == 700);
  CHECK(c.doubleClickInterval == 300);
  CHECK(c.windowPlacement == WindowPlacement::ColSmart);
  CHECK(c.workspaceCount == 6u);
  REQUIRE(c.workspaceNames.size() == 3u);
  CHECK(c.workspaceNames[0] == "one");
  CHECK(c.workspaceNames[2] == "three");
  CHECK(c.toolbar.enabled == false);
  CHECK(c.toolbar.placement == ToolbarPlacement::TopLeft);
  CHECK(c.toolbar.autoHide == true);
  CHECK(c.toolbar.alwaysOnTop == true);
  CHECK(c.toolbar.widthPercent == 90);
}

TEST_CASE("focus model: SloppyFocus carries AutoRaise/ClickRaise sub-flags") {
  Config c = parse("session.focusModel: SloppyFocus AutoRaise ClickRaise\n");
  CHECK(c.focusModel == FocusModel::SloppyFocus);
  CHECK(c.autoRaise == true);
  CHECK(c.clickRaise == true);
}

TEST_CASE("focus model: ClickToFocus forces both raise sub-flags off") {
  // Even with AutoRaise present, ClickToFocus wins and zeroes the sub-flags,
  // matching BlackboxResource::load.
  Config c = parse("session.focusModel: ClickToFocus AutoRaise\n");
  CHECK(c.focusModel == FocusModel::ClickToFocus);
  CHECK(c.autoRaise == false);
  CHECK(c.clickRaise == false);
}

TEST_CASE("focus/placement enums fall back on unknown values") {
  Config c = parse(
    "session.focusModel: GibberishFocus\n"   // not ClickToFocus -> Sloppy, no raises
    "session.windowPlacement: NopePlacement\n"
    "session.screen0.toolbar.placement: Nowhere\n");
  CHECK(c.focusModel == FocusModel::SloppyFocus);
  CHECK(c.autoRaise == false);
  CHECK(c.clickRaise == false);
  CHECK(c.windowPlacement == WindowPlacement::RowSmart);
  CHECK(c.toolbar.placement == ToolbarPlacement::BottomCenter);
}

TEST_CASE("placement and toolbar enum matching is case-insensitive") {
  Config c = parse(
    "session.windowPlacement: cascadeplacement\n"
    "session.screen0.toolbar.placement: topRIGHT\n");
  CHECK(c.windowPlacement == WindowPlacement::Cascade);
  CHECK(c.toolbar.placement == ToolbarPlacement::TopRight);
}

TEST_CASE("malformed numerics fall back to the default") {
  Config c = parse(
    "session.doubleClickInterval: notanumber\n"
    "session.screen0.workspaces: \n"
    "session.screen0.toolbar.widthPercent: abc\n");
  CHECK(c.doubleClickInterval == 250);
  CHECK(c.workspaceCount == 4u);
  CHECK(c.toolbar.widthPercent == 66);
}

TEST_CASE("workspaceNames splits on comma and keeps empty fields") {
  // A trailing comma yields a trailing empty name, exactly like the reference's
  // std::find-based split; inner whitespace is preserved (not trimmed).
  Config c = parse("session.screen0.workspaceNames: a, b,\n");
  REQUIRE(c.workspaceNames.size() == 3u);
  CHECK(c.workspaceNames[0] == "a");
  CHECK(c.workspaceNames[1] == " b");
  CHECK(c.workspaceNames[2] == "");
}

TEST_CASE("classname fallback resolves a per-screen toolbar key") {
  // Only the X-resource class form is present; bt::Resource resolves via it.
  Config c = parse("Session.screen0.Toolbar.Placement: TopCenter\n");
  CHECK(c.toolbar.placement == ToolbarPlacement::TopCenter);
}

TEST_CASE("screen index selects the matching per-screen block") {
  Config c = parse(
    "session.screen0.workspaces: 2\n"
    "session.screen1.workspaces: 9\n"
    "session.screen1.toolbar.placement: TopRight\n", /*screen=*/1);
  CHECK(c.workspaceCount == 9u);
  CHECK(c.toolbar.placement == ToolbarPlacement::TopRight);
}

TEST_CASE("Config::load reads a fixture file from disk") {
  Config c = Config::load(std::string(BBAI_TEST_FIXTURE_DIR) + "/sample.blackboxrc");
  CHECK(c.focusModel == FocusModel::SloppyFocus);
  CHECK(c.autoRaise == true);
  CHECK(c.focusNewWindows == false);
  CHECK(c.doubleClickInterval == 300);
  CHECK(c.autoRaiseDelay == 250);
  CHECK(c.windowPlacement == WindowPlacement::Cascade);
  CHECK(c.workspaceCount == 6u);
  REQUIRE(c.workspaceNames.size() == 6u);
  CHECK(c.workspaceNames[0] == "web");
  CHECK(c.workspaceNames[5] == "scratch");
  CHECK(c.toolbar.placement == ToolbarPlacement::TopCenter);
  CHECK(c.toolbar.autoHide == true);
  CHECK(c.toolbar.alwaysOnTop == true);
  CHECK(c.toolbar.widthPercent == 80);
}

TEST_CASE("Config::load on a missing file yields all defaults") {
  Config c = Config::load("/nonexistent/path/.blackboxrc");
  CHECK(c.focusModel == FocusModel::ClickToFocus);
  CHECK(c.workspaceCount == 4u);
  CHECK(c.toolbar.placement == ToolbarPlacement::BottomCenter);
}
