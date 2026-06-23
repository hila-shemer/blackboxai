#!/usr/bin/env bash
# Narrated CLI demo of blackboxai. Runs real commands in a fresh terminal.
#
#   ./demo.sh              run against the in-place build (default)
#   DEMO_PAUSE=0 ./demo.sh no read-pauses (fast replay / self-test)
#
# Stable-only: there is no installed binary on PATH and no staging/next
# channel -- blackboxai is a from-source compositor, so we demo the artifacts
# already built under build/ (stale is fine).
#
# Deliberately NO 'set -e': we want real exit codes shown on screen, not the
# script aborted. A fresh shell starts here -> absolute paths throughout.
set -uo pipefail

SRC=/home/hila/proj/blackboxai        # source dir; assumed not to move (stale ok)
PAUSE=${DEMO_PAUSE:-3}

case ${1:-} in
  '') ;;
  *) echo "usage: $0   (stable-only; no --staging/--next)" >&2; exit 2 ;;
esac

# The built artifacts we exercise. The main `blackboxai` binary is a real
# Wayland compositor -- it would block forever waiting on a seat/display, so we
# do NOT launch it here. Everything below drives the SAME code headlessly.
WM="$SRC/build/src/blackboxai"
UNIT="$SRC/build/tests/unit-tests"
DEMO="$SRC/build/tests/demo-focus-swap"

say() { printf '# %s\n' "$*"; sleep "$(( PAUSE > 0 ? 1 : 0 ))"; }   # explanation line
run() { printf '\n$ %s\n' "$*"; eval "$*"; sleep "$PAUSE"; }          # show command, run for real, pause

# --- overview: assume the viewer last saw this months ago, name alone won't do
say "blackboxai -- a from-scratch AI rewrite of the classic Blackbox window"
say "manager, but for Wayland: a C++20 wlroots 0.20 compositor that draws its"
say "own server-side titlebars, root menu, toolbar/iconbar and workspaces."
say "It renders headless via pixman, so the whole thing is testable with no GPU."
say "Demoing: in-place build at $SRC/build  (branch wayland-rewrite)"

# --- the headline: drive the real compositor logic headlessly
say "First, the unit sweep -- decorations, menus, stacking, workspaces, all"
say "exercised against the real compositor objects (no display needed):"
run "$UNIT 2>&1 | tail -4"

# The demo tests boot a real headless compositor + an in-process test client,
# replay pointer/keyboard events, and -- if BBAI_DEMO_DIR is set -- dump each
# rendered frame as a PNG. We point that at a throwaway sandbox so the run
# leaves nothing behind in the tree.
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
say "Now watch it actually composite a session. focus-swap drives two"
say "server-decorated windows and clicks their titlebars to swap focus,"
say "capturing every rendered frame to a sandbox we clean up:"
run "BBAI_DEMO_DIR=$tmp $DEMO 2>&1 | tail -3"
run "ls $tmp/focus_swap/*.png | wc -l | xargs echo 'frames the compositor rendered:'"
run "file $tmp/focus_swap/0000.png | sed 's#$tmp#<sandbox>#'"

say "Those are real composited frames (1280x720 RGBA) -- titlebars, focus"
say "gradients and all -- produced with no GPU and no monitor."
say "That's blackboxai. Source: $SRC"
