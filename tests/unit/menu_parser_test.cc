// Unit tests for the Blackbox menu-file parser (src/MenuParser). Pure: text in,
// MenuItem tree out. No wlroots, no live menu. The real reference menu.in lives
// under tests/fixtures/ and is reached via the BBAI_MENU_FIXTURE compile define.
#include <doctest/doctest.h>
#include "MenuParser.hh"
#include "Text.hh"

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

using namespace bbai;
using menuparser::Result;

namespace {
  // UTF-8 -> u32 so test literals stay readable.
  std::u32string u(const char *s) { return bt::decodeUtf8(s); }

  // The Exec mapping shells out via /bin/sh -c <command>.
  std::vector<std::string> sh(const std::string &cmd) {
    return {"/bin/sh", "-c", cmd};
  }
}

TEST_CASE("flat menu: begin title + exec/exit items map to the right actions") {
  Result r = menuparser::parse(
    "[begin] (My Menu)\n"
    "  [exec] (xterm) {xterm -ls}\n"
    "  [exec] (Editor) {vi}\n"
    "  [exit] (Quit)\n"
    "[end]\n");

  CHECK(r.title == u("My Menu"));
  REQUIRE(r.items.size() == 3);

  CHECK(r.items[0].kind == MenuItem::Kind::Command);
  CHECK(r.items[0].action == MenuItem::Act::Exec);
  CHECK(r.items[0].label == u("xterm"));
  CHECK(r.items[0].argv == sh("xterm -ls"));

  CHECK(r.items[1].action == MenuItem::Act::Exec);
  CHECK(r.items[1].label == u("Editor"));
  CHECK(r.items[1].argv == sh("vi"));

  CHECK(r.items[2].kind == MenuItem::Kind::Command);
  CHECK(r.items[2].action == MenuItem::Act::Exit);
  CHECK(r.items[2].label == u("Quit"));
}

TEST_CASE("separator and nop") {
  Result r = menuparser::parse(
    "[begin] (m)\n"
    "  [exec] (a) {a}\n"
    "  [sep]\n"
    "  [nop] (a label)\n"
    "  [nop]\n"
    "[end]\n");

  REQUIRE(r.items.size() == 4);
  CHECK(r.items[1].kind == MenuItem::Kind::Separator);
  CHECK(r.items[1].separator());

  // [nop] is an inert (disabled) command; label optional.
  CHECK(r.items[2].kind == MenuItem::Kind::Command);
  CHECK(r.items[2].action == MenuItem::Act::None);
  CHECK_FALSE(r.items[2].enabled);
  CHECK(r.items[2].label == u("a label"));
  CHECK_FALSE(r.items[2].selectable());

  CHECK_FALSE(r.items[3].enabled);
  CHECK(r.items[3].label.empty());
}

TEST_CASE("nested submenu builds the right tree, closed by [end]") {
  Result r = menuparser::parse(
    "[begin] (root)\n"
    "  [exec] (top) {top}\n"
    "  [submenu] (Graphics) {Graphics title}\n"
    "    [exec] (gimp) {gimp}\n"
    "    [submenu] (More)\n"
    "      [exec] (deep) {deep}\n"
    "    [end]\n"
    "  [end]\n"
    "  [exit] (Quit)\n"
    "[end]\n");

  REQUIRE(r.items.size() == 3);
  CHECK(r.items[0].label == u("top"));

  const MenuItem &g = r.items[1];
  CHECK(g.kind == MenuItem::Kind::Submenu);
  CHECK(g.label == u("Graphics"));
  REQUIRE(g.submenu_items.size() == 2);
  CHECK(g.submenu_items[0].action == MenuItem::Act::Exec);
  CHECK(g.submenu_items[0].label == u("gimp"));

  const MenuItem &more = g.submenu_items[1];
  CHECK(more.kind == MenuItem::Kind::Submenu);
  CHECK(more.label == u("More"));
  REQUIRE(more.submenu_items.size() == 1);
  CHECK(more.submenu_items[0].label == u("deep"));

  // The [exit] after the submenu's [end] belongs back at the top level.
  CHECK(r.items[2].action == MenuItem::Act::Exit);
}

namespace {
  bool anyDiagContains(const Result &r, const std::string &needle) {
    for (const auto &d : r.diagnostics)
      if (d.find(needle) != std::string::npos) return true;
    return false;
  }

  menuparser::FileLoader fakeLoader(std::map<std::string, std::string> files) {
    return [files](const std::string &path, std::string &text) {
      auto it = files.find(path);
      if (it == files.end()) return false;
      text = it->second;
      return true;
    };
  }

  menuparser::DirLister fakeLister(std::map<std::string, std::vector<std::string>> dirs) {
    return [dirs](const std::string &dir, std::vector<std::string> &names) {
      auto it = dirs.find(dir);
      if (it == dirs.end()) return false;
      names = it->second;
      return true;
    };
  }
}

TEST_CASE("restart: bare restarts self, {cmd} becomes RestartOther via the shell") {
  Result r = menuparser::parse(
    "[begin] (m)\n"
    "  [restart] (Restart)\n"
    "  [restart] (Start FVWM) {fvwm}\n"
    "[end]\n");

  REQUIRE(r.items.size() == 2);
  CHECK(r.items[0].action == MenuItem::Act::Restart);
  CHECK(r.items[0].label == u("Restart"));
  CHECK(r.items[0].argv.empty());

  // Classic RestartOther shape: exec the named WM through the shell, `exec`
  // so the intermediate sh is replaced.
  CHECK(r.items[1].action == MenuItem::Act::RestartOther);
  CHECK(r.items[1].label == u("Start FVWM"));
  CHECK(r.items[1].argv == sh("exec fvwm"));
  CHECK(r.diagnostics.empty());   // no longer a degradation
}

TEST_CASE("workspaces and config become MARKED placeholder submenus") {
  Result r = menuparser::parse(
    "[begin] (m)\n"
    "  [workspaces] (Workspace List)\n"
    "  [config] (Configuration)\n"
    "[end]\n");

  REQUIRE(r.items.size() == 2);

  CHECK(r.items[0].kind == MenuItem::Kind::Submenu);
  CHECK(r.items[0].action == MenuItem::Act::WorkspacesMenu);
  CHECK(r.items[0].label == u("Workspace List"));
  CHECK(r.items[0].submenu_items.empty());

  CHECK(r.items[1].kind == MenuItem::Kind::Submenu);
  CHECK(r.items[1].action == MenuItem::Act::ConfigMenu);
  CHECK(r.items[1].label == u("Configuration"));
  CHECK(r.items[1].submenu_items.empty());

  // [workspaces] is fully handled from here on - no diagnostic. [config]
  // keeps a note until wave-2 configmenu populates it.
  CHECK_FALSE(anyDiagContains(r, "workspaces"));
  CHECK(anyDiagContains(r, "config"));
}

TEST_CASE("[style] becomes a SetStyle item with a tilde-expanded path") {
  setenv("HOME", "/home/bb", 1);
  Result r = menuparser::parse(
    "[begin] (m)\n"
    "  [style] (Some Style) {~/.blackbox/styles/x}\n"
    "  [style] (Absolute) {/usr/share/styles/y}\n"
    "  [style] (no path)\n"
    "[end]\n");

  REQUIRE(r.items.size() == 2);
  CHECK(r.items[0].kind == MenuItem::Kind::Command);
  CHECK(r.items[0].action == MenuItem::Act::SetStyle);
  CHECK(r.items[0].label == u("Some Style"));
  CHECK(r.items[0].argv == std::vector<std::string>{"/home/bb/.blackbox/styles/x"});
  CHECK(r.items[1].argv == std::vector<std::string>{"/usr/share/styles/y"});
  CHECK(anyDiagContains(r, "style"));   // the path-less one degrades with a note
}

TEST_CASE("[reconfig] becomes a Reconfigure item; the documented {cmd} is dropped") {
  Result r = menuparser::parse(
    "[begin] (m)\n"
    "  [reconfig] (Reconfigure)\n"
    "  [reconfig] (With Cmd) {touch /tmp/x}\n"
    "[end]\n");

  REQUIRE(r.items.size() == 2);
  CHECK(r.items[0].action == MenuItem::Act::Reconfigure);
  CHECK(r.items[0].label == u("Reconfigure"));
  CHECK(r.items[1].action == MenuItem::Act::Reconfigure);
  CHECK(r.items[1].argv.empty());
  CHECK(anyDiagContains(r, "touch /tmp/x"));   // dropped loudly, not silently
}

TEST_CASE("[stylesdir] is still skipped with a note (wired next)") {
  Result r = menuparser::parse(
    "[begin] (m)\n"
    "  [stylesdir] (~/.blackbox/styles)\n"
    "[end]\n");
  CHECK(r.items.empty());
  CHECK(anyDiagContains(r, "stylesdir"));
}

TEST_CASE("[include] appends into the current level, even inside a submenu") {
  auto loader = fakeLoader({
    {"/menus/extra", "  [exec] (Two) {two}\n  [exec] (Three) {three}\n"},
  });
  Result r = menuparser::parse(
    "[begin] (m)\n"
    "  [exec] (One) {one}\n"
    "  [submenu] (Sub)\n"
    "    [include] (/menus/extra)\n"
    "  [end]\n"
    "[end]\n", loader);

  REQUIRE(r.items.size() == 2);
  const MenuItem &sub = r.items[1];
  REQUIRE(sub.kind == MenuItem::Kind::Submenu);
  REQUIRE(sub.submenu_items.size() == 2);          // appended INTO the submenu
  CHECK(sub.submenu_items[0].label == u("Two"));
  CHECK(r.files == std::vector<std::string>{"/menus/extra"});
}

TEST_CASE("[include] expands tilde and reports an unreadable file") {
  setenv("HOME", "/home/bb", 1);
  auto loader = fakeLoader({{"/home/bb/extra", "  [exec] (In) {in}\n"}});
  Result r = menuparser::parse(
    "[begin] (m)\n"
    "  [include] (~/extra)\n"
    "  [include] (/gone)\n"
    "[end]\n", loader);
  REQUIRE(r.items.size() == 1);
  CHECK(r.items[0].label == u("In"));
  CHECK(anyDiagContains(r, "/gone"));
  CHECK(r.files == std::vector<std::string>{"/home/bb/extra"});  // only the readable one
}

TEST_CASE("pipe includes are an explicit v1 non-goal: skipped with a note") {
  Result r = menuparser::parse(
    "[begin] (m)\n"
    "  [include] (|genmenu)\n"
    "[end]\n", fakeLoader({}));
  CHECK(r.items.empty());
  CHECK(anyDiagContains(r, "pipe"));
}

TEST_CASE("a self-including file hits the depth cap instead of spinning") {
  const std::string self =
    "[begin] (m)\n"
    "  [exec] (a) {a}\n"
    "  [include] (/menus/self)\n"
    "[end]\n";
  Result r = menuparser::parse(self, fakeLoader({{"/menus/self", self}}));
  // Top level + 16 nested includes parse; the 17th is refused. Classic would
  // spin forever here.
  CHECK(r.items.size() == 17);
  CHECK(anyDiagContains(r, "deep"));
}

TEST_CASE("parseFile records the main file first in Result::files") {
  auto loader = fakeLoader({
    {"/menus/main", "[begin] (m)\n  [include] (/menus/extra)\n[end]\n"},
    {"/menus/extra", "  [exec] (X) {x}\n"},
  });
  Result r = menuparser::parseFile("/menus/main", loader);
  REQUIRE(r.items.size() == 1);
  CHECK(r.files == std::vector<std::string>{"/menus/main", "/menus/extra"});
}

TEST_CASE("unknown tags are skipped with a note") {
  Result r = menuparser::parse(
    "[begin] (m)\n"
    "  [exec] (keep) {keep}\n"
    "  [frobnicate] (bogus)\n"
    "[end]\n");
  REQUIRE(r.items.size() == 1);
  CHECK(r.items[0].label == u("keep"));
  CHECK(anyDiagContains(r, "frobnicate"));
}

TEST_CASE("escaped delimiters survive in labels and commands") {
  Result r = menuparser::parse(
    "[begin] (m)\n"
    "  [exec] (\\(cool\\) \\{x\\}) {echo \\(hi\\)}\n"
    "[end]\n");

  REQUIRE(r.items.size() == 1);
  CHECK(r.items[0].label == u("(cool) {x}"));
  CHECK(r.items[0].argv == sh("echo (hi)"));
}

// ---- Robustness: malformed input must degrade, never crash -----------------

TEST_CASE("empty input yields an empty menu, no crash") {
  Result r = menuparser::parse("");
  CHECK(r.items.empty());
  CHECK(r.title.empty());
}

TEST_CASE("exec missing its command brace is skipped, not fatal") {
  Result r = menuparser::parse(
    "[begin] (m)\n"
    "  [exec] (no command here)\n"      // no { } field
    "  [exec] (ok) {ok}\n"
    "[end]\n");
  REQUIRE(r.items.size() == 1);
  CHECK(r.items[0].label == u("ok"));
  CHECK(anyDiagContains(r, "exec"));
}

TEST_CASE("a stray extra [end] stops cleanly without underflowing") {
  Result r = menuparser::parse(
    "[begin] (m)\n"
    "  [exec] (a) {a}\n"
    "[end]\n"
    "[end]\n"                            // unbalanced - must not crash
    "[exec] (never) {never}\n");         // past the top-level [end], ignored
  REQUIRE(r.items.size() == 1);
  CHECK(r.items[0].label == u("a"));
}

TEST_CASE("a submenu left open at EOF still keeps the items it got") {
  Result r = menuparser::parse(
    "[begin] (m)\n"
    "  [submenu] (Open)\n"
    "    [exec] (a) {a}\n"
    "    [exec] (b) {b}\n");             // no [end], no top [end] - just EOF
  REQUIRE(r.items.size() == 1);
  CHECK(r.items[0].kind == MenuItem::Kind::Submenu);
  CHECK(r.items[0].submenu_items.size() == 2);
}

TEST_CASE("garbage lines and lines without a [tag] are ignored") {
  Result r = menuparser::parse(
    "this is not a menu line\n"
    "[begin] (m)\n"
    "  # a comment with [exec] (trap) {trap}\n"
    "  plain text, no brackets\n"
    "  [exec] (real) {real}\n"
    "[end]\n");
  REQUIRE(r.items.size() == 1);
  CHECK(r.items[0].label == u("real"));
}

// ---- The real reference fixture parses with the expected shape -------------

#ifdef BBAI_MENU_FIXTURE
namespace {
  const MenuItem *findByLabel(const std::vector<MenuItem> &items, const char *lbl) {
    std::u32string want = bt::decodeUtf8(lbl);
    for (const auto &it : items)
      if (it.label == want) return &it;
    return nullptr;
  }
}

TEST_CASE("the real data/menu.in fixture parses with sane known entries") {
  Result r = menuparser::parseFile(BBAI_MENU_FIXTURE);

  CHECK(r.diagnostics.size() >= 0);    // never throws regardless of content
  CHECK(r.title == u("Blackbox"));
  REQUIRE(r.items.size() > 5);

  // First entry: [exec] (xterm) {xterm -ls}.
  CHECK(r.items[0].action == MenuItem::Act::Exec);
  CHECK(r.items[0].label == u("xterm"));
  CHECK(r.items[0].argv == sh("xterm -ls"));

  // A nested submenu: Graphics -> The GIMP {gimp}.
  const MenuItem *graphics = findByLabel(r.items, "Graphics");
  REQUIRE(graphics != nullptr);
  CHECK(graphics->kind == MenuItem::Kind::Submenu);
  const MenuItem *gimp = findByLabel(graphics->submenu_items, "The GIMP");
  REQUIRE(gimp != nullptr);
  CHECK(gimp->argv == sh("gimp"));

  // Two levels deep: Mozilla -> More... -> Mozilla Mail {mozilla -mail}.
  const MenuItem *moz = findByLabel(r.items, "Mozilla");
  REQUIRE(moz != nullptr);
  const MenuItem *more = findByLabel(moz->submenu_items, "More...");
  REQUIRE(more != nullptr);
  const MenuItem *mail = findByLabel(more->submenu_items, "Mozilla Mail");
  REQUIRE(mail != nullptr);
  CHECK(mail->argv == sh("mozilla -mail"));

  // [stylesdir] inside the Styles submenu is skipped -> empty cascade.
  const MenuItem *styles = findByLabel(r.items, "Styles");
  REQUIRE(styles != nullptr);
  CHECK(styles->submenu_items.empty());

  // [workspaces] / [config] -> placeholder submenus.
  const MenuItem *wsl = findByLabel(r.items, "Workspace List");
  REQUIRE(wsl != nullptr);
  CHECK(wsl->kind == MenuItem::Kind::Submenu);
  CHECK(wsl->submenu_items.empty());
  CHECK(wsl->action == MenuItem::Act::WorkspacesMenu);

  // The "Others" submenu is all RestartOther entries now.
  const MenuItem *others = findByLabel(r.items, "Others");
  REQUIRE(others != nullptr);
  const MenuItem *fvwm = findByLabel(others->submenu_items, "Start FVWM");
  REQUIRE(fvwm != nullptr);
  CHECK(fvwm->action == MenuItem::Act::RestartOther);
  CHECK(fvwm->argv == sh("exec fvwm"));

  // Tail actions: Restart -> Act::Restart, Exit -> Act::Exit.
  const MenuItem *restart = findByLabel(r.items, "Restart");
  REQUIRE(restart != nullptr);
  CHECK(restart->action == MenuItem::Act::Restart);
  const MenuItem *exit = findByLabel(r.items, "Exit");
  REQUIRE(exit != nullptr);
  CHECK(exit->action == MenuItem::Act::Exit);
}
#endif  // BBAI_MENU_FIXTURE
