// The keybinding table + matcher (pure; no wlroots state). Maps a
// (modifier-mask, keysym) to a WM Action. Match is by modifier EQUALITY (after
// stripping CapsLock/NumLock) so Mod4+Tab does not also fire on Mod4+Shift+Tab,
// and case-insensitive on letters. builtinDefaults() is the compiled-in table;
// loadFile() replaces it with a user's fluxbox-style keys file (session.keyFile).
#ifndef BLACKBOXAI_KEYBINDINGS_HH
#define BLACKBOXAI_KEYBINDINGS_HH

#include "wlr.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bbai {

  struct Action {
    enum Kind {
      None, WorkspaceNext, WorkspacePrev, WorkspaceTo,
      OpenMenu, CloseWindow, CycleNext, CyclePrev,
      IconMenu, Screenshot, Quit,
      ToggleFullscreen, SnapLeft, SnapRight, MoveToOutput,
      Exec              // spawn `exec` via /bin/sh -c (keys-file :Exec only)
    };
    Kind kind = None;
    int arg = 0;         // workspace index for WorkspaceTo; wlr_direction for MoveToOutput
    std::string exec;    // shell command for Exec (rest of the :Exec line, verbatim)
  };

  class Keybindings {
  public:
    struct Binding { uint32_t mods; xkb_keysym_t sym; Action action; };

    Keybindings();                                       // installs the built-in defaults
    Action dispatch(uint32_t mods, xkb_keysym_t sym) const;

    // The compiled-in default table (the historical M4 bindings). The default
    // ctor installs these; loadFile() replaces them with a user's keys file.
    static std::vector<Binding> builtinDefaults();

    // Parse one fluxbox-style line "<modifiers> <key> :<Action> [args]".
    // Returns the binding on success; nullopt with *err EMPTY for a blank or
    // '#' comment line; nullopt with *err SET describing why for a malformed
    // line. The produced (mods, sym) is in the same normalised shape dispatch()
    // matches against. err may be null.
    static std::optional<Binding> parseLine(const std::string &line,
                                             std::string *err);

    // Replace the table with the bindings parsed from `path` (fluxbox-style,
    // one per line). Malformed lines are skipped with a stderr diagnostic. The
    // reserved Ctrl+Alt+BackSpace -> Quit escape valve is installed with
    // precedence (matched first) and cannot be shadowed by the file. Returns
    // false and leaves the current table untouched if the file cannot be read.
    bool loadFile(const std::string &path);

    // CapsLock + NumLock(Mod2) are masked out before comparing.
    static uint32_t cleanMods(uint32_t mods) {
      return mods & ~(static_cast<uint32_t>(WLR_MODIFIER_CAPS) |
                      static_cast<uint32_t>(WLR_MODIFIER_MOD2));
    }

  private:
    std::vector<Binding> bindings_;
  };

} // namespace bbai

#endif // BLACKBOXAI_KEYBINDINGS_HH
