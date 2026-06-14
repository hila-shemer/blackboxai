#!/bin/sh
# Sanitize a C header so it parses as C++. Reads the header path as $1, writes
# sanitized text to stdout. Used by the wlr_scene_compat and fcft_compat
# custom_targets (see meson.build / toolkit/wlr.hpp / toolkit/text.hpp).
#
# Two C-only constructs g++ rejects even inside extern "C":
#   1. the C99 '[static N]' / '[static <ident>]' array-parameter hint
#      (wlroots wlr_scene.h uses '[static 4]'; fcft uses '[static count]' /
#       '[static len]') -> strip to '[]'.
#   2. the 'restrict' keyword (fcft_kerning's 'long *restrict x') -> drop it.
exec sed -E -e 's/\[static [a-zA-Z0-9_]+\]/[]/g' -e 's/\brestrict\b//g' "$1"
