// B1: pin the evdev->XKB +8 offset and keysym lookup, purely (no wlroots device,
// just xkbcommon) — this is the integration seam the keysym-level test injector
// (B3) bypasses, so it gets its own deterministic coverage.
#include <doctest/doctest.h>
#include "wlr.hpp"
#include "Keyboard.hh"

#include <linux/input-event-codes.h>  // KEY_TAB / KEY_ESC / KEY_1 (evdev codes)

using namespace bbai;

TEST_CASE("evdevToXkb adds the +8 offset") {
  CHECK(evdevToXkb(KEY_TAB) == KEY_TAB + 8u);
  CHECK(evdevToXkb(0) == 8u);
}

TEST_CASE("a real device-less xkb_state maps evdev+8 to the expected keysyms") {
  xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  REQUIRE(ctx != nullptr);
  xkb_keymap *km = xkb_keymap_new_from_names(ctx, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
  REQUIRE(km != nullptr);
  xkb_state *state = xkb_state_new(km);
  REQUIRE(state != nullptr);

  CHECK(xkb_state_key_get_one_sym(state, evdevToXkb(KEY_TAB)) == XKB_KEY_Tab);
  CHECK(xkb_state_key_get_one_sym(state, evdevToXkb(KEY_ESC)) == XKB_KEY_Escape);
  CHECK(xkb_state_key_get_one_sym(state, evdevToXkb(KEY_1)) == XKB_KEY_1);
  CHECK(xkb_state_key_get_one_sym(state, evdevToXkb(KEY_LEFT)) == XKB_KEY_Left);

  xkb_state_unref(state);
  xkb_keymap_unref(km);
  xkb_context_unref(ctx);
}
