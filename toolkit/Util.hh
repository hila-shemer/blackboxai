// Small path helpers ported from blackboxwm lib/Util.{hh,cc}. Only what the
// config/style/menu loaders need - not a general utility dump.
#ifndef BLACKBOXAI_UTIL_HH
#define BLACKBOXAI_UTIL_HH

#include <string>

namespace bt {

  // "~/foo" -> "$HOME/foo". Ported from lib/Util.cc:134; the reference indexes
  // s[0] on an empty string and substr's npos on a bare "~" - both fixed here.
  // Classic semantics kept otherwise: anything from '~' to the first '/' is
  // replaced by $HOME (so "~user/x" does NOT do per-user lookup, same as classic).
  std::string expandTilde(const std::string &path);

} // namespace bt

#endif // BLACKBOXAI_UTIL_HH
