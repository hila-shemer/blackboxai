// The pure half of XDG autostart: parse a .desktop's [Desktop Entry] group,
// decide whether it should run under our desktop id, and strip the Exec line's
// %-field codes. Deliberately filesystem-free and wlroots-free — the dir glob,
// TryExec/PATH probe, and the actual spawn live in the Server wiring (M7 Task 6)
// so this layer stays unit-testable on plain strings.
#ifndef BLACKBOXAI_AUTOSTART_HH
#define BLACKBOXAI_AUTOSTART_HH

#include <string>
#include <vector>

namespace bbai {

  struct DesktopEntry {
    std::string exec;
    std::string try_exec;
    bool hidden = false;
    std::vector<std::string> only_show_in;
    std::vector<std::string> not_show_in;
  };

  // Parse the [Desktop Entry] group only; other groups are ignored. Splits each
  // line on the first '='; last value for a key wins; unknown keys are dropped.
  DesktopEntry parseDesktopEntry(const std::string &contents);

  // True unless the entry is Hidden, or OnlyShowIn is non-empty and lacks
  // current_desktop, or NotShowIn contains current_desktop. (TryExec/PATH is the
  // wiring layer's job — kept out of the pure predicate.)
  bool shouldAutostart(const DesktopEntry &e, const std::string &current_desktop);

  // Drop the Exec field codes (%f %F %u %U %i %c %k %d %D %n %N %v %m), turn a
  // literal "%%" into "%", collapse the resulting runs of spaces, and trim.
  std::string stripFieldCodes(const std::string &exec);

} // namespace bbai

#endif // BLACKBOXAI_AUTOSTART_HH
