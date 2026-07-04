// tests/system/configmenu_test.cc
// The Configuration submenu end-to-end: mount from a [config] menu file,
// click a row -> config_ flips in memory + persists to the rc with the
// classic spelling, reopen shows fresh checkmarks, disabled rows are dead.
// Click coordinates come from the menu's own hit-test accessors, never baked
// margin math - the menus slice may swap the pinned margins for style values
// and these tests must survive that re-bless without a rewrite (synthesis
// obligation). Every rc sets focusModel EXPLICITLY: window-mgmt flips the
// key-less default to SloppyFocus (locked) and lands before us.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "Server.hh"
#include "Menu.hh"
#include "Config.hh"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <linux/input-event-codes.h>

using namespace bbai;

namespace {
  const char *kRc = "/tmp/bbai-configmenu-test.rc";
  const char *kMenu = "/tmp/bbai-configmenu-test.menu";

  void writeFile(const char *p, const std::string &body) {
    std::ofstream f(p, std::ios::trunc);
    f << body;
  }
  std::string slurp(const char *p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
  }
  void writeMenu() {
    writeFile(kMenu, "[begin] (Test)\n  [config] (Configuration)\n[end]\n");
  }
  void boot(Server &server) {
    REQUIRE(server.ok());
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
      server.dispatch();
    REQUIRE(server.titleFont()->height() == 18);  // gotcha #20: pinned font or the rows shift
  }

  // Row-center Y derived from the menu's own hit-test: scan the Y extent for
  // the band mapping to idx. itemIndexAtGlobal excludes separators only, so
  // disabled rows are findable too.
  int rowX(const Menu *m) { return m->rectXForTest() + 5; }
  int rowCenterY(const Menu *m, int idx) {
    int top = -1, bot = -1;
    for (int y = m->rectYForTest(); y < m->rectYForTest() + 600; ++y) {
      if (m->itemIndexAtGlobal(rowX(m), y) == idx) { if (top < 0) top = y; bot = y; }
      else if (top >= 0) break;
    }
    REQUIRE(top >= 0);
    return (top + bot) / 2;
  }
  void hoverRow(Server &server, const Menu *m, int idx) {   // hover-open handles cascades
    server.injectPointerMotionForTest(rowX(m), rowCenterY(m, idx));
  }
  void clickRow(Server &server, const Menu *m, int idx) {
    hoverRow(server, m, idx);
    server.injectPointerButtonForTest(BTN_LEFT, true);
  }

  // Right-click the desktop, hover-open Configuration (row 0). Child rows:
  // 0 Focus Model, 1 Window Placement, 2 separator, 3 Focus New Windows.
  Menu *openConfigMenu(Server &server) {
    server.injectPointerMotionForTest(600, 150);
    server.injectPointerButtonForTest(BTN_RIGHT, true);
    REQUIRE(server.menuOpenForTest());
    Menu *root = server.rootMenuForTest();
    hoverRow(server, root, 0);
    REQUIRE(root->submenuOpenForTest());
    return root->child();
  }
}

TEST_CASE("Focus New Windows: click flips config, persists the classic spelling, reopen unchecks") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  writeFile(kRc, "session.focusModel: ClickToFocus\n"
                 "session.focusNewWindows: True\n");
  writeMenu();

  Server server(/*headless=*/true, kRc);
  boot(server);
  server.setMenuFileForTest(kMenu);

  Menu *cfg = openConfigMenu(server);
  REQUIRE(cfg->itemCount() == 4);
  CHECK(cfg->item(3).checked);

  clickRow(server, cfg, 3);
  CHECK_FALSE(server.menuOpenForTest());           // toggle closes the chain (classic hideAll)
  CHECK_FALSE(server.config().focusNewWindows);    // in-memory flip, no reconfigure()
  CHECK(slurp(kRc).find("session.focusNewWindows: False\n") != std::string::npos);
  // updateRcKey rewrites ONE line - the user's other key survives verbatim.
  CHECK(slurp(kRc).find("session.focusModel: ClickToFocus\n") != std::string::npos);

  cfg = openConfigMenu(server);                    // rebuilt per open: fresh checkmarks
  CHECK_FALSE(cfg->item(3).checked);
  server.injectKeyForTest(XKB_KEY_Escape, 0, true);
  std::remove(kRc);
  std::remove(kMenu);
}

TEST_CASE("focus radios: Sloppy enables the raise rows; the composite spelling accretes") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  writeFile(kRc, "session.focusModel: ClickToFocus\n");
  writeMenu();

  Server server(/*headless=*/true, kRc);
  boot(server);
  server.setMenuFileForTest(kMenu);

  Menu *cfg = openConfigMenu(server);
  hoverRow(server, cfg, 0);                        // Focus Model hover-opens its cascade
  REQUIRE(cfg->submenuOpenForTest());
  Menu *fm = cfg->child();
  REQUIRE(fm->itemCount() == 4);
  CHECK(fm->item(0).checked);                      // ClickToFocus radio
  CHECK_FALSE(fm->item(2).enabled);                // AutoRaise locked out under CTF

  clickRow(server, fm, 1);                         // Sloppy Focus
  CHECK_FALSE(server.menuOpenForTest());
  CHECK(server.config().focusModel == FocusModel::SloppyFocus);
  CHECK(slurp(kRc).find("session.focusModel: SloppyFocus\n") != std::string::npos);

  cfg = openConfigMenu(server);
  hoverRow(server, cfg, 0);
  fm = cfg->child();
  CHECK(fm->item(1).checked);
  REQUIRE(fm->item(2).enabled);                    // now live
  clickRow(server, fm, 2);                         // Auto Raise on
  CHECK(server.config().autoRaise);
  CHECK(slurp(kRc).find("session.focusModel: SloppyFocus AutoRaise\n") != std::string::npos);
  std::remove(kRc);
  std::remove(kMenu);
}

TEST_CASE("placement radio: Cascade persists the classic *Placement spelling") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  writeFile(kRc, "session.focusModel: ClickToFocus\n"
                 "session.windowPlacement: RowSmartPlacement\n");
  writeMenu();

  Server server(/*headless=*/true, kRc);
  boot(server);
  server.setMenuFileForTest(kMenu);

  Menu *cfg = openConfigMenu(server);
  hoverRow(server, cfg, 1);                        // Window Placement cascade
  REQUIRE(cfg->submenuOpenForTest());
  Menu *wp = cfg->child();
  CHECK(wp->item(0).checked);                      // RowSmart from the rc
  clickRow(server, wp, 3);                         // Cascade
  CHECK(server.config().windowPlacement == WindowPlacement::Cascade);
  CHECK(slurp(kRc).find("session.windowPlacement: CascadePlacement\n") != std::string::npos);
  std::remove(kRc);
  std::remove(kMenu);
}

TEST_CASE("a disabled row neither dispatches nor dismisses (classic dead row)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  writeFile(kRc, "session.focusModel: ClickToFocus\n");
  writeMenu();

  Server server(/*headless=*/true, kRc);
  boot(server);
  server.setMenuFileForTest(kMenu);

  Menu *cfg = openConfigMenu(server);
  hoverRow(server, cfg, 0);
  Menu *fm = cfg->child();
  REQUIRE_FALSE(fm->item(2).enabled);              // Auto Raise, dead under CTF
  clickRow(server, fm, 2);
  CHECK(server.menuOpenForTest());                 // chain stays up
  CHECK_FALSE(server.config().autoRaise);          // nothing dispatched
  CHECK(slurp(kRc).find("AutoRaise") == std::string::npos);
  server.injectKeyForTest(XKB_KEY_Escape, 0, true);
  std::remove(kRc);
  std::remove(kMenu);
}

TEST_CASE("no rc: the toggle flips in memory, persist is a guarded no-op") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  writeMenu();

  Server server(/*headless=*/true);                // rc_path_ empty: headless never discovers HOME
  boot(server);
  server.setMenuFileForTest(kMenu);

  const bool before = server.config().focusNewWindows;
  Menu *cfg = openConfigMenu(server);
  clickRow(server, cfg, 3);
  CHECK(server.config().focusNewWindows == !before);   // live flip, no file, no crash
  std::remove(kMenu);
}

TEST_CASE("golden: Configuration submenu + Focus Model cascade (builtin style)") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  // Explicit focus model: merge-stable across window-mgmt's default flip,
  // and it puts a checkmark + one disabled pair in frame (drawCheck gutter,
  // frameDisabled text - the two renderings this slice leans on).
  writeFile(kRc, "session.focusModel: SloppyFocus AutoRaise\n");
  writeMenu();

  Server server(/*headless=*/true, kRc);
  boot(server);
  server.setMenuFileForTest(kMenu);

  Menu *cfg = openConfigMenu(server);
  hoverRow(server, cfg, 0);                        // Focus Model cascade open
  REQUIRE(cfg->submenuOpenForTest());
  CHECK(test::compareGolden(test::captureFrame(server),
                            "tests/golden/v1-configmenu.png", 2, 40));
  server.injectKeyForTest(XKB_KEY_Escape, 0, true);
  std::remove(kRc);
  std::remove(kMenu);
}
