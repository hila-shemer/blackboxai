// Parses a Blackbox menu file (the classic `[tag] (label) {data}` grammar, see
// reference/blackboxwm/data/README.menu) into the project's existing MenuItem
// tree. PARSE LAYER ONLY: text/path -> std::vector<MenuItem>; it neither touches
// the filesystem beyond the one file it is handed, nor wires anything into the
// live root menu.
//
// Mapping onto the existing MenuItem representation (no new menu types invented):
//   [begin] (title)        -> Result::title (not an item)
//   [end]                  -> closes the current (sub)menu level
//   [exec]  (l) {cmd}       -> Command + Act::Exec, argv = {"/bin/sh","-c",cmd}
//   [exit]  (l)             -> Command + Act::Exit
//   [restart] (l) {cmd?}    -> Command + Act::Restart (the alternate-WM {cmd} has
//                              no RestartOther action here; it is dropped + noted)
//   [submenu] (l) {title?}  -> Submenu (children parsed to the matching [end]);
//                              {title} has no MenuItem field - the cascade titles
//                              itself from the label, so a distinct title is noted
//   [sep]/[separator]       -> Separator
//   [nop]   (l?)            -> disabled Command (inert spacer/label)
//   [workspaces]/[config]   -> empty placeholder Submenu (runtime-populated at
//                              wire-up; the parser cannot enumerate workspaces)
//   [style]/[stylesdir]/[include]/[reconfig]/unknown -> skipped + noted (no
//                              backing action/behavior in the compositor)
//
// The tokenizer mirrors blackbox's string_within: backslash escapes the next
// character inside a field, a leading '#' is a comment, malformed lines are
// skipped rather than fatal. Every degraded/skipped line is recorded in
// Result::diagnostics so the caller can surface or ignore them.
#ifndef BLACKBOXAI_MENUPARSER_HH
#define BLACKBOXAI_MENUPARSER_HH

#include "MenuItem.hh"

#include <string>
#include <vector>

namespace bbai::menuparser {

  struct Result {
    std::u32string title;                  // [begin] title; empty if none given
    std::vector<MenuItem> items;           // parsed top-level tree
    std::vector<std::string> diagnostics;  // one note per degraded/skipped line
  };

  // Parse the whole menu-file text. Never throws; malformed input degrades.
  Result parse(const std::string &text);

  // Read `path` and parse it. On open failure returns an empty Result carrying a
  // single diagnostic (no throw).
  Result parseFile(const std::string &path);

} // namespace bbai::menuparser

#endif // BLACKBOXAI_MENUPARSER_HH
