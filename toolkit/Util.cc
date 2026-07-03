#include "Util.hh"

#include <cstdlib>

namespace bt {

  std::string expandTilde(const std::string &s) {
    if (s.empty() || s[0] != '~') return s;
    const char *home = std::getenv("HOME");
    if (!home) return s;
    const size_t slash = s.find('/');
    if (slash == std::string::npos) return home;   // bare "~"
    return std::string(home) + s.substr(slash);
  }

} // namespace bt
