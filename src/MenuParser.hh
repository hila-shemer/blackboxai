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
//   [restart] (l) {cmd?}    -> Command + Act::Restart (bare: restart self) or
//                              Act::RestartOther, argv={"/bin/sh","-c","exec "+cmd}
//   [submenu] (l) {title?}  -> Submenu (children parsed to the matching [end]);
//                              {title} has no MenuItem field - the cascade titles
//                              itself from the label, so a distinct title is noted
//   [sep]/[separator]       -> Separator
//   [nop]   (l?)            -> disabled Command (inert spacer/label)
//   [workspaces]/[config]   -> empty placeholder Submenu (runtime-populated at
//                              wire-up; the parser cannot enumerate workspaces)
//   [style]  (l) {path}     -> Command + Act::SetStyle, argv[0]=expandTilde(path)
//   [reconfig] (l) {cmd?}   -> Command + Act::Reconfigure ({cmd} dropped + noted)
//   [include] (path)        -> included items appended into the CURRENT level
//                              (tilde-expanded; pipe '|cmd' skipped + noted;
//                              depth-capped at 16; path recorded in Result::files)
//   [stylesdir] (dir)       -> inline SetStyle items, one per regular file
//   [stylesmenu] (l) {dir}  -> the same listing wrapped in a titled Submenu
//   unknown                 -> skipped + noted
//
// The tokenizer mirrors blackbox's string_within: backslash escapes the next
// character inside a field, a leading '#' is a comment, malformed lines are
// skipped rather than fatal. Every degraded/skipped line is recorded in
// Result::diagnostics so the caller can surface or ignore them.
#ifndef BLACKBOXAI_MENUPARSER_HH
#define BLACKBOXAI_MENUPARSER_HH

#include "MenuItem.hh"

#include <functional>
#include <string>
#include <vector>

namespace bbai::menuparser {

  // Injectable filesystem seams so [include]/[stylesdir] stay pure-testable.
  // FileLoader: read `path` into `text`; false = unreadable or not a regular
  // file. DirLister: fill `names` with the REGULAR files in `dir` (names only,
  // any order); false = missing or not a directory.
  using FileLoader = std::function<bool(const std::string &path, std::string &text)>;
  using DirLister  = std::function<bool(const std::string &dir, std::vector<std::string> &names)>;

  FileLoader defaultFileLoader();  // fopen + S_ISREG (classic [include] check)
  DirLister  defaultDirLister();   // opendir/readdir + S_ISREG per entry

  struct Result {
    std::u32string title;                  // [begin] title; empty if none given
    std::vector<MenuItem> items;           // parsed top-level tree
    std::vector<std::string> diagnostics;  // one note per degraded/skipped line
    std::vector<std::string> files;        // main file + every [include]d file +
                                           // stylesdirs - the stat-on-open reload set
  };

  // Parse the whole menu-file text. Never throws; malformed input degrades.
  Result parse(const std::string &text,
               const FileLoader &loader = defaultFileLoader(),
               const DirLister &lister = defaultDirLister());

  // Load `path` through `loader` and parse it; `path` lands first in
  // Result::files. On open failure returns an empty Result carrying a single
  // diagnostic (no throw).
  Result parseFile(const std::string &path,
                   const FileLoader &loader = defaultFileLoader(),
                   const DirLister &lister = defaultDirLister());

} // namespace bbai::menuparser

#endif // BLACKBOXAI_MENUPARSER_HH
