# Installing BlackboxAI

BlackboxAI pins wlroots 0.20, which today resolves only on Fedora 44. There are
two ways in: the COPR package, or a build from source. Read section A's warning
before you try anything on a non-Fedora box.

## A. Build from source - two realities

BlackboxAI needs wlroots **0.20.x**. Two distributions carry it: a Fedora-44
host, or a Fedora-44 container. Ubuntu and Debian ship wlroots 0.19.x, so
`meson setup` fails on the version pin (`>=0.20.0,<0.21.0` in `meson.build`).

> **Do not run `meson setup` on a stock Ubuntu/Debian box.** It will fail on the
> wlroots 0.20 pin, and no amount of `apt install` fixes it while the archive is
> on 0.19. Use a Fedora-44 host or the container below.

### 1. Fedora-44 container (the canonical path)

CI builds in a `fedora:44` container, so it is the reference environment. Mount
your checkout and build inside it:

```
docker run --rm --shm-size=1g -v "$PWD":/src -w /src fedora:44 bash -c '
  dnf install -y meson ninja-build gcc-c++ pkgconf-pkg-config sed \
    wlroots-devel wayland-devel wayland-protocols-devel pixman-devel \
    libdrm-devel libxkbcommon-devel libpng-devel fcft-devel systemd-devel &&
  meson setup build &&
  ninja -C build'
```

`--shm-size=1g` is load-bearing if you run the **test suite**: the default 64 MB
`/dev/shm` is too small for screen-sized `wl_shm` buffers and the tests
`SIGBUS`. For a plain compositor build it does no harm; keep it for parity. Tests
also need a headless GL stack and are gated behind `-Dtests=true` (off by
default).

### 2. Fedora-44 host

On a Fedora 44 machine, install the same dependencies and build natively:

```
sudo dnf install meson ninja-build gcc-c++ pkgconf-pkg-config sed \
  wlroots-devel wayland-devel wayland-protocols-devel pixman-devel \
  libdrm-devel libxkbcommon-devel libpng-devel fcft-devel systemd-devel
meson setup build
ninja -C build
sudo ninja -C build install
```

(`gcovr`, `git`, and `dbus-daemon` from the CI job are coverage- and test-only;
a plain build does not need them.)

## B. Install via COPR

Fedora 44 only:

```
sudo dnf copr enable hila-shemer/blackboxai
sudo dnf install blackboxai
```

The package installs:

- `/usr/bin/blackboxai` - the compositor
- `/usr/share/wayland-sessions/blackboxai.desktop` - the GDM session entry
- `/usr/share/blackboxai/styles/` - the 19 bundled styles
- `/usr/share/blackboxai/menu` - the starter menu (editable)
- `man blackboxai` - the manual page

## C. Pick it at GDM

Log out, click the gear on the GDM greeter, choose **BlackboxAI**, and log in.
The greeter reads the session from the `.desktop` file installed above. The
mechanics - and what to check when the session does not show up - are in
[gdm-session.md](gdm-session.md).

## D. Autostart

At startup BlackboxAI scans `~/.config/autostart` and then `/etc/xdg/autostart`
for `.desktop` files and launches them, with the user directory shadowing the
system one by basename. It sets `XDG_CURRENT_DESKTOP=Blackbox` before spawning,
so child programs resolve `OnlyShowIn` / `NotShowIn` against it:

- To run something only under BlackboxAI, add `OnlyShowIn=Blackbox;`.
- `Hidden=true` or `NotShowIn=Blackbox;` suppresses an entry.
- A `TryExec=` that is not on `PATH` skips the entry silently.

BlackboxAI ships no screen locker or idle daemon - locking and idle are
protocol-only (`ext-session-lock-v1` + `ext-idle-notify-v1`). Autostart
`swayidle`, and use `swaylock` as the locker; while a lock is up,
`Ctrl+Alt+Backspace` is suppressed (see [gdm-session.md](gdm-session.md)).
