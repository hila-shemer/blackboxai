// The single C-interop boundary for fcft (font rasterization) + pixman. Parallel
// to toolkit/wlr.hpp, kept SEPARATE from it: fcft is an unrelated C library with
// its own pixman/harfbuzz/freetype dep chain and its own C++-incompatible
// constructs, so folding it into wlr.hpp would drag fcft's transitive includes
// into every TU that only needs wlroots.
//
// fcft.h uses the C99 '[static count]'/'[static len]' array-parameter hint and
// the C-only 'restrict' keyword, both of which g++ rejects even inside
// extern "C". We include a build-time *sanitized* copy (<fcft_compat.h>, produced
// by the fcft_compat custom_target in the top-level meson.build via
// tools/sanitize-c-header.sh). Because fcft.h is '#pragma once' rather than
// guarded, the copy is given a distinct name and is the ONLY fcft header the
// tree includes — nothing else pulls <fcft/fcft.h>, so there is no collision.
#ifndef BLACKBOXAI_TEXT_HPP
#define BLACKBOXAI_TEXT_HPP

extern "C" {
#include <fcft_compat.h>
}

#endif // BLACKBOXAI_TEXT_HPP
