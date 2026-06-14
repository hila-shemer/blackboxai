// The M4 built-in keybinding table + matcher (pure; no wlroots state). Maps a
// (modifier-mask, keysym) to a WM Action. Match is by modifier EQUALITY (after
// stripping CapsLock/NumLock) so Mod4+Tab does not also fire on Mod4+Shift+Tab,
// and case-insensitive on letters. Drop-in .blackboxrc keybinding parsing is M5;
// these defaults are hardcoded.
#ifndef BLACKBOXAI_KEYBINDINGS_HH
#define BLACKBOXAI_KEYBINDINGS_HH

#include "wlr.hpp"

#include <cstdint>
#include <vector>

namespace bbai {

  struct Action {
    enum Kind {
      None, WorkspaceNext, WorkspacePrev, WorkspaceTo,
      OpenMenu, CloseWindow, CycleNext, CyclePrev
    };
    Kind kind = None;
    int arg = 0;     // workspace index for WorkspaceTo
  };

  class Keybindings {
  public:
    Keybindings();                                       // installs the M4 defaults
    Action dispatch(uint32_t mods, xkb_keysym_t sym) const;

    // CapsLock + NumLock(Mod2) are masked out before comparing.
    static uint32_t cleanMods(uint32_t mods) {
      return mods & ~(static_cast<uint32_t>(WLR_MODIFIER_CAPS) |
                      static_cast<uint32_t>(WLR_MODIFIER_MOD2));
    }

  private:
    struct Binding { uint32_t mods; xkb_keysym_t sym; Action action; };
    std::vector<Binding> bindings_;
  };

} // namespace bbai

#endif // BLACKBOXAI_KEYBINDINGS_HH
