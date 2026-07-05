# The GDM session

How BlackboxAI appears at the greeter, how to escape a wedged session, and what
to check when it does not show up.

## How the session is listed

The package installs `data/blackboxai.desktop` to
`/usr/share/wayland-sessions/blackboxai.desktop`. GDM reads that directory to
build the session list. Three fields carry three different spellings of one
name, so it is worth being explicit:

| Field | Value | Role |
| --- | --- | --- |
| `Name` | `BlackboxAI` | The label shown at the greeter. |
| `Exec` | `blackboxai` | The binary GDM runs, resolved via `/usr/bin` on `PATH`. |
| `DesktopNames` | `Blackbox` | Sets `XDG_CURRENT_DESKTOP=Blackbox` for the whole session. |

There is no `blackbox` binary and no `Blackbox` session label - the running
binary is `blackboxai`, the greeter label is `BlackboxAI`, and `Blackbox` is only
the desktop-environment identity string that autostart and portals match against.

(`desktop-file-validate` will flag `DesktopNames` as an unknown key. That is a
limitation of the generic validator, not a fault in the file: `DesktopNames` is
the standard key display managers read for session files. The package's `%check`
tolerates exactly that one complaint.)

## The escape hatch

`Ctrl+Alt+Backspace` quits the compositor. It is the Wayland analogue of the
classic X-server kill and exists to escape a wedged session and drop back to the
greeter.

It is **suppressed while a session lock is up** - locked means locked, so the
key does nothing until the lock clears. If the locker itself has wedged, the way
out is a kernel-side VT switch (`Ctrl+Alt+F2` to another virtual terminal, log
in there, and kill the stuck process), not a compositor keybinding.

## Troubleshooting

**The session is missing from the greeter.** The `.desktop` was not installed, or
landed under the wrong prefix. Confirm it is at
`/usr/share/wayland-sessions/blackboxai.desktop`. A source build with the default
prefix installs to `/usr/local/share/wayland-sessions/`, which most GDM builds do
**not** scan - install with `--prefix=/usr` (the RPM does this) or point your GDM
at the local prefix.

**The session bounces straight back to the greeter.** The compositor started and
exited. It usually means it could not initialise - no seat, or no DRM master
(another compositor already holds the GPU). Check the journal:

```
journalctl --user -b -e
```

**Multi-head and VT switching.** BlackboxAI supports multiple outputs, and a
`Ctrl+Alt+Fn` VT switch away and back leaves a running session intact.
