#!/bin/sh
# Strip the C99 '[static N]' array-parameter hint from a wlroots header so it
# parses as C++. Reads the header path as $1, writes sanitized text to stdout.
# Used by the wlr_scene_compat custom_target (see meson.build / toolkit/wlr.hpp).
exec sed -E 's/\[static [0-9]+\]/[]/g' "$1"
