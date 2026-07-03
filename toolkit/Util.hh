// Small pure path helpers shared by the config and menu layers. rc-style owns
// this file (it lands first on the merge train); menu-wire bootstraps an
// identical copy when developing ahead of it - on rebase, take rc-style's.
#ifndef BLACKBOXAI_TOOLKIT_UTIL_HH
#define BLACKBOXAI_TOOLKIT_UTIL_HH

#include <string>

namespace bt {

  // "~" or "~/..." -> $HOME-prefixed path. Anything else (including "~user")
  // passes through untouched, like the reference bt::expandTilde - minus its
  // crash on a bare "~".
  std::string expandTilde(const std::string &path);

} // namespace bt

#endif // BLACKBOXAI_TOOLKIT_UTIL_HH
