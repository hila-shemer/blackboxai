# COPR (owner hila-shemer, fedora-44 only - wlroots ABI breaks every minor, and
# f44 is the chroot carrying wlroots 0.20.x):
#   copr-cli create blackboxai --chroot fedora-44-x86_64
#   # then an SCM/git build against this spec, or:
#   copr-cli build blackboxai blackboxai-0.1.0-1.src.rpm
# Users:
#   sudo dnf copr enable hila-shemer/blackboxai
#   sudo dnf install blackboxai
#   # log out, pick "BlackboxAI" at the GDM session gear.
Name:           blackboxai
Version:        0.1.0
Release:        1%{?dist}
Summary:        Blackbox window manager, reborn as a Wayland compositor

License:        MIT
URL:            https://github.com/hila-shemer/blackboxai
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  meson
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  sed
BuildRequires:  pkgconfig(wlroots-0.20)
BuildRequires:  wayland-devel
BuildRequires:  wayland-protocols-devel
BuildRequires:  pkgconfig(pixman-1)
BuildRequires:  pkgconfig(libdrm)
BuildRequires:  pkgconfig(xkbcommon)
BuildRequires:  pkgconfig(fcft) >= 3.0.0
BuildRequires:  pkgconfig(libsystemd)
BuildRequires:  pkgconfig(libpng)
# doctest is vendored (third_party/); the test tree is built only with
# -Dtests=true, which this package does not set - so no doctest-devel.

# Session locking and idle are protocol-only (ext-session-lock-v1 +
# ext-idle-notify-v1); the compositor ships no locker. swaylock is the
# recommended external locker, hence a soft dep, not a hard Requires.
Recommends:     swaylock

%description
BlackboxAI is a from-scratch Wayland compositor that reproduces the classic
Blackbox window-manager identity - texture and gradient theming, the toolbar,
the root, window, and workspace menus, workspaces, and the slit - with drop-in
.blackboxrc, style-file, and menu-file compatibility. wlroots 0.20, C++20.

Pick "BlackboxAI" at your display-manager greeter after install.

%prep
%autosetup

%build
# Tests need a headless GL stack + a large /dev/shm the COPR builders do not
# provide; CI covers the test gate. Build the compositor only.
%meson -Dtests=false
%meson_build

%install
%meson_install

%check
# No meson test here (see %%build). A cheap validity check on the shipped
# session file catches a malformed .desktop. desktop-file-validate is strict
# about session-only keys: DesktopNames - the key GDM reads to set
# XDG_CURRENT_DESKTOP - is legitimate in a wayland-sessions file, but
# desktop-file-utils rejects it as an unknown extension. Validate anyway and
# tolerate only that one known false positive; any other complaint still fails.
sessionfile=%{buildroot}%{_datadir}/wayland-sessions/%{name}.desktop
out=$(desktop-file-validate "$sessionfile" || :)
echo "$out"
echo "$out" | grep -v 'DesktopNames' | grep -q . && exit 1 || :

%files
%license LICENSE
%doc README.md
%{_bindir}/blackboxai
%{_datadir}/wayland-sessions/blackboxai.desktop
%dir %{_datadir}/blackboxai
%{_datadir}/blackboxai/menu
%{_datadir}/blackboxai/styles/
%{_mandir}/man1/blackboxai.1*

%changelog
* Sun Jul 05 2026 Hila Shemer <nadav.shemer@gmail.com> - 0.1.0-1
- Initial package (productize-v1 Wave 3).
