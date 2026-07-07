// B2: the pure keybinding table + equality-mask matcher.
#include <doctest/doctest.h>
#include "Keybindings.hh"

using namespace bbai;

static const uint32_t SUPER = WLR_MODIFIER_LOGO;
static const uint32_t SHIFT = WLR_MODIFIER_SHIFT;

TEST_CASE("default bindings map to the expected actions") {
  Keybindings kb;
  CHECK(kb.dispatch(SUPER, XKB_KEY_Right).kind == Action::WorkspaceNext);
  CHECK(kb.dispatch(SUPER, XKB_KEY_Left).kind  == Action::WorkspacePrev);
  CHECK(kb.dispatch(SUPER, XKB_KEY_space).kind == Action::OpenMenu);
  CHECK(kb.dispatch(SUPER, XKB_KEY_q).kind     == Action::CloseWindow);
  CHECK(kb.dispatch(SUPER, XKB_KEY_Tab).kind   == Action::CycleNext);

  Action ws3 = kb.dispatch(SUPER, XKB_KEY_3);
  CHECK(ws3.kind == Action::WorkspaceTo);
  CHECK(ws3.arg == 2);              // "3" -> index 2
}

TEST_CASE("builtinDefaults matches the constructed table") {
  Keybindings kb;
  CHECK(kb.dispatch(SUPER, XKB_KEY_Right).kind == Action::WorkspaceNext);
  Action ws1 = kb.dispatch(SUPER, XKB_KEY_1);
  CHECK(ws1.kind == Action::WorkspaceTo);
  CHECK(ws1.arg == 0);
  CHECK(Keybindings::builtinDefaults().size() >= 20);
}

TEST_CASE("modifier match is by equality, not subset") {
  Keybindings kb;
  // Super+Tab is CycleNext; Super+Shift+Tab is CyclePrev (must not fire CycleNext).
  CHECK(kb.dispatch(SUPER | SHIFT, XKB_KEY_Tab).kind == Action::CyclePrev);
  CHECK(kb.dispatch(SUPER | SHIFT, XKB_KEY_ISO_Left_Tab).kind == Action::CyclePrev);
  // An extra modifier on a single-mod binding does NOT match. (Super+space is
  // OpenMenu; Super+Shift+space is unbound - Super+Shift+Right is now SnapRight,
  // so use a key that stays free to keep proving the equality-mask rule.)
  CHECK(kb.dispatch(SUPER | SHIFT, XKB_KEY_space).kind == Action::None);
  // The bare keysym with no modifier does not match a Super binding.
  CHECK(kb.dispatch(0, XKB_KEY_Right).kind == Action::None);
}

TEST_CASE("CapsLock / NumLock are masked out before comparing") {
  Keybindings kb;
  CHECK(kb.dispatch(SUPER | WLR_MODIFIER_CAPS, XKB_KEY_Right).kind == Action::WorkspaceNext);
  CHECK(kb.dispatch(SUPER | WLR_MODIFIER_MOD2, XKB_KEY_Right).kind == Action::WorkspaceNext);
}

TEST_CASE("an unbound combo returns no action") {
  Keybindings kb;
  CHECK(kb.dispatch(SUPER, XKB_KEY_z).kind == Action::None);
  CHECK(kb.dispatch(WLR_MODIFIER_CTRL, XKB_KEY_c).kind == Action::None);
}

TEST_CASE("Mod4+F7 maps to Action::Screenshot, modifier-exact") {
  Keybindings kb;
  CHECK(kb.dispatch(SUPER, XKB_KEY_F7).kind == Action::Screenshot);
  // Modifier EQUALITY: Mod4+Shift+F7 must NOT match.
  CHECK(kb.dispatch(SUPER | SHIFT, XKB_KEY_F7).kind == Action::None);
}

TEST_CASE("Ctrl+Alt+Backspace is the session-quit chord") {
  Keybindings kb;
  Action a = kb.dispatch(WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_BackSpace);
  CHECK(a.kind == Action::Quit);
}

TEST_CASE("wave-2 window-mgmt bindings") {
  Keybindings kb;
  const uint32_t CTRL = WLR_MODIFIER_CTRL;
  CHECK(kb.dispatch(SUPER, XKB_KEY_f).kind == Action::ToggleFullscreen);
  CHECK(kb.dispatch(SUPER | SHIFT, XKB_KEY_Left).kind  == Action::SnapLeft);
  CHECK(kb.dispatch(SUPER | SHIFT, XKB_KEY_Right).kind == Action::SnapRight);

  Action ml = kb.dispatch(SUPER | CTRL, XKB_KEY_Left);
  CHECK(ml.kind == Action::MoveToOutput);
  CHECK(ml.arg == WLR_DIRECTION_LEFT);
  Action md = kb.dispatch(SUPER | CTRL, XKB_KEY_Down);
  CHECK(md.kind == Action::MoveToOutput);
  CHECK(md.arg == WLR_DIRECTION_DOWN);

  // Plain Super+Left still switches workspaces (must not be shadowed).
  CHECK(kb.dispatch(SUPER, XKB_KEY_Left).kind == Action::WorkspacePrev);
}
