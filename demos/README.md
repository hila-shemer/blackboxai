# BlackboxAI Demo Videos

Short demo clips of the compositor's four core interaction features. Each clip
is ~3 fps, encoded from headless+pixman golden frames captured by the `demo`
test suite.

Run `tools/make-demos.sh` from the repository root to regenerate all videos.
The `.mp4` files are gitignored (they are artifacts, not source).

## Clips

### focus_swap.mp4

Two SSD windows side-by-side. A titlebar click on window A focuses it (active
gradient); a second click on window B swaps focus. Demonstrates the
focused/unfocused titlebar palette difference.

### cascade_menu.mp4

Right-click on the bare desktop opens the root menu. Hovering the "Workspaces"
row cascades a child submenu to the right. Hovering a child workspace row
highlights it. Pressing Escape closes the whole chain.

### window_ops.mp4

A single SSD window goes through: initial state → maximize (fills work area) →
restore (geometry back) → iconify (window hides) → open icon menu → click entry
to deiconify (window reappears, focused).

### toolbar_autohide.mp4

Auto-hide enabled: the toolbar collapses to a 2 px sliver. Moving the pointer
onto the sliver reveals the full bar; moving away hides it again after the
debounce delay.
