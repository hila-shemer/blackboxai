// Keyboard input for M4: the evdev->XKB keycode helper (the +8 offset is the
// load-bearing detail — wlroots key events carry evdev keycodes, every xkb_*
// call wants XKB keycodes = evdev+8), plus the RAII per-device Keyboard that
// wires wlr_keyboard.events.key/modifiers into Server::onKey/onModifiers (added
// in M4-B3).
#ifndef BLACKBOXAI_KEYBOARD_HH
#define BLACKBOXAI_KEYBOARD_HH

#include "wlr.hpp"
#include "listener.hpp"

#include <cstdint>

namespace bbai {

  class Server;

  // evdev keycode -> XKB keycode for xkb_state_key_get_syms etc.
  inline uint32_t evdevToXkb(uint32_t evdev_keycode) { return evdev_keycode + 8; }

  // RAII per-device keyboard: wires key/modifiers/destroy into the Server, torn
  // down (listeners disconnected) when the device dies. Real devices only —
  // never created under the headless test backend.
  struct Keyboard {
    Keyboard(Server &server, wlr_keyboard *kb);
    Server &server;
    wlr_keyboard *kb;
    bt::Listener key, modifiers, destroy;
  };

} // namespace bbai

#endif // BLACKBOXAI_KEYBOARD_HH
