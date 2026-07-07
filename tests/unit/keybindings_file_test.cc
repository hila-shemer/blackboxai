// The fluxbox-style keys-file parser + loader (no main; smoke_test.cc carries
// the DOCTEST main for the unit exe).
#include <doctest/doctest.h>
#include "Keybindings.hh"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using bbai::Keybindings;
using bbai::Action;

static Action parseAction(const std::string &line) {
  std::string err;
  auto b = Keybindings::parseLine(line, &err);
  REQUIRE_MESSAGE(b.has_value(), err);
  return b->action;
}

static std::string writeTmp(const std::string &body) {
  std::string p = std::string(std::tmpnam(nullptr)) + ".keys";
  std::ofstream(p) << body;
  return p;
}

TEST_CASE("modifiers: aliases and fluxbox tokens both resolve") {
  std::string err;
  CHECK(Keybindings::parseLine("Super Right :NextWorkspace", &err)->mods == WLR_MODIFIER_LOGO);
  CHECK(Keybindings::parseLine("Mod4 Right :NextWorkspace", &err)->mods == WLR_MODIFIER_LOGO);
  CHECK(Keybindings::parseLine("Mod Right :NextWorkspace", &err)->mods == WLR_MODIFIER_LOGO);
  CHECK(Keybindings::parseLine("Alt Tab :NextWindow", &err)->mods == WLR_MODIFIER_ALT);
  CHECK(Keybindings::parseLine("Mod1 Tab :NextWindow", &err)->mods == WLR_MODIFIER_ALT);
  CHECK(Keybindings::parseLine("ctrl alt BackSpace :Quit", &err)->mods
        == (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT));
  CHECK(Keybindings::parseLine("None space :RootMenu", &err)->mods == 0);
  CHECK(Keybindings::parseLine("Super Shift Left :SnapLeft", &err)->mods
        == (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT));
}

TEST_CASE("keysym resolves case-insensitively") {
  std::string err;
  CHECK(Keybindings::parseLine("Super f :ToggleFullscreen", &err)->sym == XKB_KEY_f);
  CHECK(Keybindings::parseLine("Super F :ToggleFullscreen", &err)->sym == XKB_KEY_f);
  CHECK(Keybindings::parseLine("Super F7 :Screenshot", &err)->sym == XKB_KEY_F7);
  CHECK(Keybindings::parseLine("Super space :RootMenu", &err)->sym == XKB_KEY_space);
}

TEST_CASE("actions and args") {
  CHECK(parseAction("Super Right :NextWorkspace").kind == Action::WorkspaceNext);
  CHECK(parseAction("Super Left :PrevWorkspace").kind == Action::WorkspacePrev);
  CHECK(parseAction("Super space :RootMenu").kind == Action::OpenMenu);
  CHECK(parseAction("Super Alt t :IconMenu").kind == Action::IconMenu);
  CHECK(parseAction("Super q :Close").kind == Action::CloseWindow);
  CHECK(parseAction("Alt Tab :NextWindow").kind == Action::CycleNext);
  CHECK(parseAction("Alt Shift Tab :PrevWindow").kind == Action::CyclePrev);
  CHECK(parseAction("Super f :ToggleFullscreen").kind == Action::ToggleFullscreen);
  CHECK(parseAction("Super Shift Left :SnapLeft").kind == Action::SnapLeft);
  CHECK(parseAction("Super Shift Right :SnapRight").kind == Action::SnapRight);
  CHECK(parseAction("Super F7 :Screenshot").kind == Action::Screenshot);
  CHECK(parseAction("ctrl alt BackSpace :Quit").kind == Action::Quit);

  // action name is case-insensitive
  CHECK(parseAction("Super q :close").kind == Action::CloseWindow);

  // Workspace N is 1-based in the file, 0-based internally
  Action ws = parseAction("Super 2 :Workspace 2");
  CHECK(ws.kind == Action::WorkspaceTo);
  CHECK(ws.arg == 1);

  // MoveToOutput direction arg
  Action mo = parseAction("Super Control Left :MoveToOutput Left");
  CHECK(mo.kind == Action::MoveToOutput);
  CHECK(mo.arg == WLR_DIRECTION_LEFT);
  CHECK(parseAction("Super Control Down :MoveToOutput down").arg == WLR_DIRECTION_DOWN);
}

TEST_CASE("Exec captures the command verbatim (spaces preserved)") {
  std::string err;
  auto b = Keybindings::parseLine("Super e :Exec thunar --new-window ~/dir", &err);
  REQUIRE_MESSAGE(b.has_value(), err);
  CHECK(b->action.kind == Action::Exec);
  CHECK(b->action.exec == "thunar --new-window ~/dir");
  // extra spaces after :Exec are trimmed from the front of the command
  CHECK(Keybindings::parseLine("Super e :Exec    kitty", &err)->action.exec == "kitty");
}

TEST_CASE("Exec with no command is malformed") {
  std::string err;
  CHECK_FALSE(Keybindings::parseLine("Super e :Exec", &err).has_value());
  CHECK_FALSE(err.empty());
  CHECK_FALSE(Keybindings::parseLine("Super e :Exec   ", &err).has_value());
}

TEST_CASE("blank and comment lines are silently skipped") {
  std::string err;
  CHECK_FALSE(Keybindings::parseLine("", &err).has_value());     CHECK(err.empty());
  CHECK_FALSE(Keybindings::parseLine("   \t ", &err).has_value()); CHECK(err.empty());
  CHECK_FALSE(Keybindings::parseLine("# a comment", &err).has_value()); CHECK(err.empty());
  CHECK_FALSE(Keybindings::parseLine("   # indented comment", &err).has_value()); CHECK(err.empty());
}

TEST_CASE("malformed lines report an error") {
  std::string err;
  CHECK_FALSE(Keybindings::parseLine("Super :NextWorkspace", &err).has_value()); // no key
  CHECK_FALSE(err.empty());
  CHECK_FALSE(Keybindings::parseLine("Super Right NextWorkspace", &err).has_value()); // no colon
  CHECK_FALSE(Keybindings::parseLine("Bogus q :NextWorkspace", &err).has_value()); // bad mod
  CHECK_FALSE(Keybindings::parseLine("Super q :Frobnicate", &err).has_value());   // bad action
  CHECK_FALSE(Keybindings::parseLine("Super q :Workspace", &err).has_value());    // missing arg
  CHECK_FALSE(Keybindings::parseLine("Super q :Workspace 0", &err).has_value());  // N<1
  CHECK_FALSE(Keybindings::parseLine("Super q :MoveToOutput sideways", &err).has_value()); // bad dir
  CHECK_FALSE(Keybindings::parseLine("Super zzzznotakey :Quit", &err).has_value()); // bad keysym
}

TEST_CASE("loadFile replaces the table (authoritative)") {
  Keybindings kb;
  auto p = writeTmp("Super n :NextWorkspace\n# comment\nSuper p :PrevWorkspace\n");
  REQUIRE(kb.loadFile(p));
  CHECK(kb.dispatch(WLR_MODIFIER_LOGO, XKB_KEY_n).kind == Action::WorkspaceNext);
  CHECK(kb.dispatch(WLR_MODIFIER_LOGO, XKB_KEY_p).kind == Action::WorkspacePrev);
  // a default the file did not set no longer fires
  CHECK(kb.dispatch(WLR_MODIFIER_LOGO, XKB_KEY_Right).kind == Action::None);
  std::remove(p.c_str());
}

TEST_CASE("reserved Ctrl+Alt+BackSpace wins even if the file rebinds that chord") {
  Keybindings kb;
  auto p = writeTmp("Control Alt BackSpace :NextWorkspace\n");
  REQUIRE(kb.loadFile(p));
  CHECK(kb.dispatch(WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_BackSpace).kind
        == Action::Quit);
  std::remove(p.c_str());
}

TEST_CASE("malformed lines are skipped, valid ones kept") {
  Keybindings kb;
  auto p = writeTmp("Super n :NextWorkspace\nthis is garbage\nSuper p :PrevWorkspace\n");
  REQUIRE(kb.loadFile(p));
  CHECK(kb.dispatch(WLR_MODIFIER_LOGO, XKB_KEY_n).kind == Action::WorkspaceNext);
  CHECK(kb.dispatch(WLR_MODIFIER_LOGO, XKB_KEY_p).kind == Action::WorkspacePrev);
  std::remove(p.c_str());
}

TEST_CASE("unreadable file returns false and keeps the current table") {
  Keybindings kb;
  CHECK_FALSE(kb.loadFile("/no/such/keys/file/anywhere"));
  CHECK(kb.dispatch(WLR_MODIFIER_LOGO, XKB_KEY_Right).kind == Action::WorkspaceNext);
}
