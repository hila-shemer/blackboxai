// M7: the pure XDG-autostart layer — parse a [Desktop Entry] group, decide
// whether it runs under our desktop id, and strip Exec field codes. No
// filesystem here (the dir glob + TryExec/PATH check live in the Server wiring).
#include <doctest/doctest.h>
#include "Autostart.hh"
using namespace bbai;

TEST_CASE("parseDesktopEntry pulls the [Desktop Entry] keys") {
  DesktopEntry e = parseDesktopEntry(
    "[Desktop Entry]\nType=Application\nExec=nm-applet --indicator\n"
    "OnlyShowIn=GNOME;Blackbox;\nTryExec=nm-applet\n"
    "[Other]\nExec=should-be-ignored\n");
  CHECK(e.exec == "nm-applet --indicator");
  CHECK(e.try_exec == "nm-applet");
  CHECK(e.only_show_in == std::vector<std::string>{"GNOME", "Blackbox"});
  CHECK_FALSE(e.hidden);
}

TEST_CASE("shouldAutostart honors Hidden / OnlyShowIn / NotShowIn") {
  DesktopEntry base; base.exec = "x";
  CHECK(shouldAutostart(base, "Blackbox"));                       // no constraints -> run
  DesktopEntry hidden = base; hidden.hidden = true;
  CHECK_FALSE(shouldAutostart(hidden, "Blackbox"));
  DesktopEntry only = base; only.only_show_in = {"GNOME"};
  CHECK_FALSE(shouldAutostart(only, "Blackbox"));                 // not listed -> skip
  only.only_show_in = {"GNOME", "Blackbox"};
  CHECK(shouldAutostart(only, "Blackbox"));                       // listed -> run
  DesktopEntry no = base; no.not_show_in = {"Blackbox"};
  CHECK_FALSE(shouldAutostart(no, "Blackbox"));
}

TEST_CASE("stripFieldCodes removes %-codes, keeps %%") {
  CHECK(stripFieldCodes("foo %U --bar %i") == "foo --bar");
  CHECK(stripFieldCodes("baz %%literal") == "baz %literal");
}
