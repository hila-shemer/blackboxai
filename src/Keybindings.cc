#include "Keybindings.hh"

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>

namespace bbai {

  namespace {
    std::string lower(std::string s) {
      for (char &c : s) c = static_cast<char>(std::tolower((unsigned char)c));
      return s;
    }

    // Accumulate one modifier token's bit into `out`. "None" contributes no
    // bit but is still valid. Returns false for an unknown token.
    bool modToken(const std::string &tok, uint32_t &out) {
      const std::string k = lower(tok);
      if (k == "mod4" || k == "super" || k == "mod") { out |= WLR_MODIFIER_LOGO;  return true; }
      if (k == "mod1" || k == "alt")                 { out |= WLR_MODIFIER_ALT;   return true; }
      if (k == "control" || k == "ctrl")             { out |= WLR_MODIFIER_CTRL;  return true; }
      if (k == "shift")                              { out |= WLR_MODIFIER_SHIFT; return true; }
      if (k == "mod5")                               { out |= WLR_MODIFIER_MOD5;  return true; }
      if (k == "none")                               { return true; }
      return false;
    }

    bool dirToken(const std::string &tok, int &out) {
      const std::string k = lower(tok);
      if (k == "left")  { out = WLR_DIRECTION_LEFT;  return true; }
      if (k == "right") { out = WLR_DIRECTION_RIGHT; return true; }
      if (k == "up")    { out = WLR_DIRECTION_UP;    return true; }
      if (k == "down")  { out = WLR_DIRECTION_DOWN;  return true; }
      return false;
    }

    // Case-fold, then fold ISO_Left_Tab (the keysym a physical Shift+Tab emits)
    // onto Tab, so a keys-file "Shift Tab" binding - which names the Tab keysym -
    // matches the real shifted press. The SHIFT modifier bit still distinguishes
    // Tab from Shift+Tab, so this only unifies the same physical key.
    xkb_keysym_t canonSym(xkb_keysym_t sym) {
      sym = xkb_keysym_to_lower(sym);
      if (sym == XKB_KEY_ISO_Left_Tab) sym = XKB_KEY_Tab;
      return sym;
    }
  } // namespace

  std::vector<Keybindings::Binding> Keybindings::builtinDefaults() {
    const uint32_t SUPER = WLR_MODIFIER_LOGO;
    const uint32_t ALT   = WLR_MODIFIER_ALT;
    const uint32_t SHIFT = WLR_MODIFIER_SHIFT;
    const uint32_t CTRL  = WLR_MODIFIER_CTRL;
    return {
      { SUPER,         XKB_KEY_Right, { Action::WorkspaceNext } },
      { SUPER,         XKB_KEY_Left,  { Action::WorkspacePrev } },
      { SUPER,         XKB_KEY_1,     { Action::WorkspaceTo, 0 } },
      { SUPER,         XKB_KEY_2,     { Action::WorkspaceTo, 1 } },
      { SUPER,         XKB_KEY_3,     { Action::WorkspaceTo, 2 } },
      { SUPER,         XKB_KEY_4,     { Action::WorkspaceTo, 3 } },
      { SUPER,         XKB_KEY_space, { Action::OpenMenu } },
      { SUPER,         XKB_KEY_q,     { Action::CloseWindow } },
      { ALT,           XKB_KEY_Tab,   { Action::CycleNext } },
      { ALT | SHIFT,   XKB_KEY_Tab,         { Action::CyclePrev } },
      { ALT | SHIFT,   XKB_KEY_ISO_Left_Tab,{ Action::CyclePrev } },  // real Shift+Tab sym
      { SUPER,         XKB_KEY_Tab,   { Action::CycleNext } },        // alias
      { SUPER | SHIFT, XKB_KEY_Tab,         { Action::CyclePrev } },  // alias
      { SUPER | SHIFT, XKB_KEY_ISO_Left_Tab,{ Action::CyclePrev } },  // alias
      { SUPER | ALT,   XKB_KEY_t,     { Action::IconMenu } },
      { SUPER,         XKB_KEY_F7,    { Action::Screenshot } },
      { CTRL | ALT,    XKB_KEY_BackSpace, { Action::Quit } },  // escape a wedged session
      { SUPER,         XKB_KEY_f,     { Action::ToggleFullscreen } },
      { SUPER | SHIFT, XKB_KEY_Left,  { Action::SnapLeft } },
      { SUPER | SHIFT, XKB_KEY_Right, { Action::SnapRight } },
      { SUPER | CTRL,  XKB_KEY_Left,  { Action::MoveToOutput, WLR_DIRECTION_LEFT } },
      { SUPER | CTRL,  XKB_KEY_Right, { Action::MoveToOutput, WLR_DIRECTION_RIGHT } },
      { SUPER | CTRL,  XKB_KEY_Up,    { Action::MoveToOutput, WLR_DIRECTION_UP } },
      { SUPER | CTRL,  XKB_KEY_Down,  { Action::MoveToOutput, WLR_DIRECTION_DOWN } },
    };
  }

  Keybindings::Keybindings() { bindings_ = builtinDefaults(); }

  std::optional<Keybindings::Binding>
  Keybindings::parseLine(const std::string &line, std::string *err) {
    auto fail = [&](const char *m) { if (err) *err = m; return std::optional<Binding>{}; };
    if (err) err->clear();

    // Blank or '#' comment -> silent skip (nullopt, err empty).
    const size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos || line[start] == '#') return {};
    const size_t colon = line.find(':', start);
    if (colon == std::string::npos) return fail("no ':action'");

    // Left of the colon: modifiers... then the key (the last token).
    std::istringstream ls(line.substr(start, colon - start));
    std::vector<std::string> left;
    for (std::string t; ls >> t;) left.push_back(t);
    if (left.empty()) return fail("no key before ':'");

    uint32_t mods = 0;
    for (size_t i = 0; i + 1 < left.size(); ++i)
      if (!modToken(left[i], mods)) return fail("unknown modifier");

    const xkb_keysym_t sym =
        xkb_keysym_from_name(left.back().c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
    if (sym == XKB_KEY_NoSymbol) return fail("unknown key");

    // Right of the colon: action name then optional args.
    std::istringstream rs(line.substr(colon + 1));
    std::string name;
    rs >> name;
    const std::string a = lower(name);
    Action act{};
    if      (a == "nextworkspace")    act.kind = Action::WorkspaceNext;
    else if (a == "prevworkspace")    act.kind = Action::WorkspacePrev;
    else if (a == "rootmenu")         act.kind = Action::OpenMenu;
    else if (a == "iconmenu")         act.kind = Action::IconMenu;
    else if (a == "close")            act.kind = Action::CloseWindow;
    else if (a == "nextwindow")       act.kind = Action::CycleNext;
    else if (a == "prevwindow")       act.kind = Action::CyclePrev;
    else if (a == "togglefullscreen") act.kind = Action::ToggleFullscreen;
    else if (a == "snapleft")         act.kind = Action::SnapLeft;
    else if (a == "snapright")        act.kind = Action::SnapRight;
    else if (a == "screenshot")       act.kind = Action::Screenshot;
    else if (a == "quit")             act.kind = Action::Quit;
    else if (a == "workspace") {
      int n;
      if (!(rs >> n) || n < 1) return fail("Workspace needs N>=1");
      act.kind = Action::WorkspaceTo;
      act.arg  = n - 1;                       // file is 1-based; index is 0-based
    } else if (a == "movetooutput") {
      std::string d; int dir;
      if (!(rs >> d) || !dirToken(d, dir)) return fail("MoveToOutput needs a direction");
      act.kind = Action::MoveToOutput;
      act.arg  = dir;
    } else if (a == "exec") {
      // The command is the rest of the line after the "Exec" token, verbatim
      // (may contain spaces/quotes) - not tokenised, since /bin/sh -c parses it.
      // Read it from the same stream that already consumed "Exec", so leading
      // whitespace after the colon (": Exec cmd") cannot bleed into the command.
      std::string cmd;
      std::getline(rs, cmd);
      const size_t cmd0 = cmd.find_first_not_of(" \t");
      if (cmd0 == std::string::npos) return fail("Exec needs a command");
      act.kind = Action::Exec;
      act.exec = cmd.substr(cmd0);
    } else {
      return fail("unknown action");
    }
    return Binding{ mods, sym, act };
  }

  bool Keybindings::loadFile(const std::string &path) {
    std::ifstream in(path);
    if (!in) return false;

    std::vector<Binding> next;
    // Reserved escape valve, matched first (dispatch() returns the first hit),
    // so a keys file can neither shadow nor drop it.
    next.push_back({ WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_BackSpace,
                     { Action::Quit } });

    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
      ++lineno;
      if (!line.empty() && line.back() == '\r') line.pop_back();  // tolerate CRLF
      std::string err;
      if (auto b = parseLine(line, &err))
        next.push_back(*b);
      else if (!err.empty())
        std::cerr << "keys: " << path << ":" << lineno << ": " << err
                  << " (skipped)\n";
    }
    bindings_ = std::move(next);
    return true;
  }

  Action Keybindings::dispatch(uint32_t mods, xkb_keysym_t sym) const {
    const uint32_t m = cleanMods(mods);
    const xkb_keysym_t s = canonSym(sym);
    for (const Binding &b : bindings_)
      if (cleanMods(b.mods) == m && canonSym(b.sym) == s)
        return b.action;
    return {};
  }

} // namespace bbai
