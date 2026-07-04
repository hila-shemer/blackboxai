#include <doctest/doctest.h>
#include "Config.hh"

#include <cstdlib>

using bbai::Config;
using bbai::FocusModel;
using bbai::WindowPlacement;
using bbai::toolbar::Placement;

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
  CHECK(c.toolbar.placement == Placement::BottomCenter);
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
  CHECK(c.toolbar.placement == Placement::TopLeft);
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
  CHECK(c.toolbar.placement == Placement::BottomCenter);
}

TEST_CASE("placement and toolbar enum matching is case-insensitive") {
  Config c = parse(
    "session.windowPlacement: cascadeplacement\n"
    "session.screen0.toolbar.placement: topRIGHT\n");
  CHECK(c.windowPlacement == WindowPlacement::Cascade);
  CHECK(c.toolbar.placement == Placement::TopRight);
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
  CHECK(c.toolbar.placement == Placement::TopCenter);
}

TEST_CASE("screen index selects the matching per-screen block") {
  Config c = parse(
    "session.screen0.workspaces: 2\n"
    "session.screen1.workspaces: 9\n"
    "session.screen1.toolbar.placement: TopRight\n", /*screen=*/1);
  CHECK(c.workspaceCount == 9u);
  CHECK(c.toolbar.placement == Placement::TopRight);
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
  CHECK(c.toolbar.placement == Placement::TopCenter);
  CHECK(c.toolbar.autoHide == true);
  CHECK(c.toolbar.alwaysOnTop == true);
  CHECK(c.toolbar.widthPercent == 80);
}

TEST_CASE("Config::load on a missing file yields all defaults") {
  Config c = Config::load("/nonexistent/path/.blackboxrc");
  CHECK(c.focusModel == FocusModel::ClickToFocus);
  CHECK(c.workspaceCount == 4u);
  CHECK(c.toolbar.placement == Placement::BottomCenter);
}

TEST_CASE("style/menu file keys expand tilde; absent keys take the compiled defaults") {
  setenv("HOME", "/home/tester", 1);
  Config c = parse(
    "session.styleFile: ~/.blackbox/styles/Night\n"
    "session.menuFile:  ~/.bbmenu\n");
  CHECK(c.styleFile == "/home/tester/.blackbox/styles/Night");
  CHECK(c.menuFile == "/home/tester/.bbmenu");

  Config d = parse("");
  // Defaults come from the meson defines (install paths); pin the tails.
  CHECK(d.styleFile.find("styles/Results") != std::string::npos);
  CHECK(d.menuFile.find("blackboxai/menu") != std::string::npos);
}

TEST_CASE("rc rootCommand and strftimeFormat parse") {
  Config c = parse(
    "rootCommand: feh --bg-fill ~/wall.png\n"
    "session.screen0.strftimeFormat: %H:%M\n");
  CHECK(c.rootCommand == "feh --bg-fill ~/wall.png");   // NOT tilde-expanded; /bin/sh gets it whole
  CHECK(c.strftimeFormat == "%H:%M");
  Config d = parse("");
  CHECK(d.rootCommand.empty());
  CHECK(d.strftimeFormat == "%I:%M %p");
}

TEST_CASE("slit.* pre-parse (wave-2 slit never opens Config.cc)") {
  using bbai::SlitPlacement;
  using bbai::SlitDirection;
  Config c = parse(
    "session.screen0.slit.placement: TopCenter\n"
    "session.screen0.slit.direction: Horizontal\n"
    "session.screen0.slit.onTop: True\n"
    "session.screen0.slit.autoHide: True\n");
  CHECK(c.slit.placement == SlitPlacement::TopCenter);
  CHECK(c.slit.direction == SlitDirection::Horizontal);
  CHECK(c.slit.alwaysOnTop == true);
  CHECK(c.slit.autoHide == true);
  Config d = parse("");
  CHECK(d.slit.placement == SlitPlacement::CenterRight);   // reference default
  CHECK(d.slit.direction == SlitDirection::Vertical);
  CHECK(d.slit.alwaysOnTop == false);
  CHECK(d.slit.autoHide == false);
}

TEST_CASE("mouse-wheel bools: classic keys, classic default True") {
  // Verified against reference/blackboxwm/src/BlackboxResource.cc:197-208 -
  // BOTH default true. A wrong default silently changes desktop-scroll for
  // every existing rc, so this is pinned, not guessed.
  Config d = parse("");
  CHECK(d.changeWorkspaceWithMouseWheel == true);
  CHECK(d.toolbarActionsWithMouseWheel == true);

  Config off = parse("session.changeWorkspaceWithMouseWheel: False\n"
                     "session.toolbarActionsWithMouseWheel: False\n");
  CHECK(off.changeWorkspaceWithMouseWheel == false);
  CHECK(off.toolbarActionsWithMouseWheel == false);
}

#include <cstdio>
#include <fstream>
#include <sstream>

namespace {
  std::string slurp(const std::string &p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
  }
}

TEST_CASE("updateRcKey: replace-in-place, append, create") {
  const std::string p = "/tmp/bbai-updaterc-test.rc";
  std::remove(p.c_str());

  // create
  CHECK(bbai::updateRcKey(p, "session.styleFile", "/a/b/Night"));
  CHECK(slurp(p) == "session.styleFile: /a/b/Night\n");

  // replace in place, other lines untouched (incl. a comment mentioning the key)
  {
    std::ofstream f(p);
    f << "! session.styleFile: not a setting\n"
      << "session.focusModel: SloppyFocus\n"
      << "session.styleFile:   /old/style\n"
      << "session.menuFile: ~/menu\n";
  }
  CHECK(bbai::updateRcKey(p, "session.styleFile", "/new/style"));
  CHECK(slurp(p) ==
        "! session.styleFile: not a setting\n"
        "session.focusModel: SloppyFocus\n"
        "session.styleFile: /new/style\n"
        "session.menuFile: ~/menu\n");

  // append when absent
  CHECK(bbai::updateRcKey(p, "session.workspaces", "6"));
  CHECK(slurp(p).find("session.workspaces: 6\n") != std::string::npos);
  std::remove(p.c_str());
}

#include <sys/stat.h>
#include <unistd.h>

TEST_CASE("updateRcKey: atomic replace - never truncate-in-place") {
  const std::string p = "/tmp/bbai-updaterc-atomic.rc";
  const std::string hard = p + ".hardlink";
  std::remove(p.c_str());
  std::remove(hard.c_str());
  {
    std::ofstream f(p);
    f << "session.styleFile: /old/style\n";
  }

  // Pin the mechanism through its one userspace-visible effect: a hard link.
  // In-place truncate writes through the shared inode (the link would see the
  // new content); temp+rename swaps the inode, so the link keeps the old
  // bytes. This is also exactly why a crash mid-write can't clobber the rc.
  REQUIRE(::link(p.c_str(), hard.c_str()) == 0);
  CHECK(bbai::updateRcKey(p, "session.styleFile", "/new/style"));
  CHECK(slurp(p) == "session.styleFile: /new/style\n");
  CHECK(slurp(hard) == "session.styleFile: /old/style\n");

  // no temp residue after a successful update
  CHECK(::access((p + ".tmp").c_str(), F_OK) != 0);
  std::remove(p.c_str());
  std::remove(hard.c_str());
}

TEST_CASE("updateRcKey: a symlinked rc is updated through, not replaced by a file") {
  // Dotfile managers symlink ~/.blackboxrc; the rename must land on the
  // target, or the first style pick silently detaches the rc from the repo.
  const std::string target = "/tmp/bbai-updaterc-target.rc";
  const std::string link = "/tmp/bbai-updaterc-link.rc";
  std::remove(target.c_str());
  std::remove(link.c_str());
  {
    std::ofstream f(target);
    f << "session.focusModel: SloppyFocus\n";
  }
  REQUIRE(::symlink(target.c_str(), link.c_str()) == 0);

  CHECK(bbai::updateRcKey(link, "session.styleFile", "/a/Night"));
  struct stat st{};
  REQUIRE(::lstat(link.c_str(), &st) == 0);
  CHECK(S_ISLNK(st.st_mode));   // still a symlink
  CHECK(slurp(target).find("session.styleFile: /a/Night\n") != std::string::npos);
  CHECK(slurp(target).find("session.focusModel: SloppyFocus\n") != std::string::npos);
  std::remove(link.c_str());
  std::remove(target.c_str());
}

TEST_CASE("updateRcKey: failure leaves the original untouched") {
  // A write-only rc reads as zero lines; the old code silently replaced the
  // whole file with the single new entry. Refuse instead. (Skipped as root -
  // permission checks don't bind, and the CI container runs as root.)
  if (::geteuid() != 0) {
    const std::string p = "/tmp/bbai-updaterc-wonly.rc";
    std::remove(p.c_str());
    {
      std::ofstream f(p);
      f << "session.focusModel: SloppyFocus\n";
    }
    REQUIRE(::chmod(p.c_str(), 0200) == 0);
    CHECK_FALSE(bbai::updateRcKey(p, "session.styleFile", "/x"));
    REQUIRE(::chmod(p.c_str(), 0600) == 0);
    CHECK(slurp(p) == "session.focusModel: SloppyFocus\n");
    std::remove(p.c_str());
  }

  // Unwritable destination (a directory in the way): false, no residue.
  const std::string dir = "/tmp/bbai-updaterc-dir.rc";
  ::rmdir(dir.c_str());
  REQUIRE(::mkdir(dir.c_str(), 0700) == 0);
  CHECK_FALSE(bbai::updateRcKey(dir, "session.styleFile", "/x"));
  CHECK(::access((dir + ".tmp").c_str(), F_OK) != 0);
  REQUIRE(::rmdir(dir.c_str()) == 0);
}
