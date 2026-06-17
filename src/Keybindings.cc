#include "Keybindings.hh"

namespace bbai {

  Keybindings::Keybindings() {
    const uint32_t SUPER = WLR_MODIFIER_LOGO;
    const uint32_t ALT   = WLR_MODIFIER_ALT;
    const uint32_t SHIFT = WLR_MODIFIER_SHIFT;
    bindings_ = {
      { SUPER,         XKB_KEY_Right, { Action::WorkspaceNext } },
      { SUPER,         XKB_KEY_Left,  { Action::WorkspacePrev } },
      { SUPER,         XKB_KEY_1,     { Action::WorkspaceTo, 0 } },
      { SUPER,         XKB_KEY_2,     { Action::WorkspaceTo, 1 } },
      { SUPER,         XKB_KEY_3,     { Action::WorkspaceTo, 2 } },
      { SUPER,         XKB_KEY_4,     { Action::WorkspaceTo, 3 } },
      { SUPER,         XKB_KEY_space, { Action::OpenMenu } },
      { SUPER,         XKB_KEY_q,     { Action::CloseWindow } },
      { SUPER,         XKB_KEY_Tab,   { Action::CycleNext } },
      { SUPER | SHIFT, XKB_KEY_Tab,         { Action::CyclePrev } },
      { SUPER | SHIFT, XKB_KEY_ISO_Left_Tab,{ Action::CyclePrev } },  // real Shift+Tab sym
      { SUPER | ALT,   XKB_KEY_t,     { Action::IconMenu } },
    };
  }

  Action Keybindings::dispatch(uint32_t mods, xkb_keysym_t sym) const {
    const uint32_t m = cleanMods(mods);
    const xkb_keysym_t s = xkb_keysym_to_lower(sym);
    for (const Binding &b : bindings_)
      if (cleanMods(b.mods) == m && xkb_keysym_to_lower(b.sym) == s)
        return b.action;
    return {};
  }

} // namespace bbai
