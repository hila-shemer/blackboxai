#!/usr/bin/env bash
# Run a command inside the blackboxai-ci:f44 container (the host has no wlroots,
# so meson/ninja/tests only work here). A large /dev/shm is load-bearing: the
# pixman headless system suite mmaps screen-sized wl_shm buffers and SIGBUSes
# when docker's default 64 MB /dev/shm is exhausted under parallel tests.
#
# shm-size is a `docker run` flag, not image metadata, so it must be passed on
# every run - that is what this wrapper is for.
#
#   tools/in-ci.sh 'ninja -C build-f44 && meson test -C build-f44 --suite unit'
#
# Env overrides: BBAI_SHM (default 16g), BBAI_CI_IMAGE (default blackboxai-ci:f44).
set -euo pipefail
repo="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
exec docker run --rm --shm-size="${BBAI_SHM:-16g}" \
  -v "$repo:$repo" -w "$repo" "${BBAI_CI_IMAGE:-blackboxai-ci:f44}" \
  bash -lc "$*"
