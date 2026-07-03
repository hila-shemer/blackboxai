#include "Util.hh"

#include <cstdlib>

namespace bt {

  std::string expandTilde(const std::string &path) {
    if (path.empty() || path[0] != '~') return path;
    if (path.size() > 1 && path[1] != '/') return path;  // "~user" unsupported, like reference
    const char *home = std::getenv("HOME");
    if (!home) return path;
    return std::string(home) + path.substr(1);
  }

} // namespace bt
