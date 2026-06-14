#include "Keyboard.hh"
#include "Server.hh"

namespace bbai {

  Keyboard::Keyboard(Server &s, wlr_keyboard *k) : server(s), kb(k) {
    key.connect(&kb->events.key, [this](void *data) {
      auto *e = static_cast<wlr_keyboard_key_event *>(data);
      server.onKey(kb, e->time_msec, e->keycode, e->state);
    });
    modifiers.connect(&kb->events.modifiers, [this](void *) {
      server.onModifiers(kb);
    });
    destroy.connect(&kb->base.events.destroy, [this](void *) {
      server.removeKeyboard(this);  // erases the owning unique_ptr (RAII disconnect)
    });
  }

} // namespace bbai
