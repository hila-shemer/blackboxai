project('blackboxai', ['c', 'cpp'],
  version : '0.1.0',
  default_options : ['cpp_std=c++20', 'warning_level=3', 'werror=false'])

add_project_arguments('-DWLR_USE_UNSTABLE', language : 'cpp')

cc = meson.get_compiler('cpp')

# Vendored single-header unit framework: third_party/doctest/doctest/doctest.h
# (Fedora has no doctest-devel; vendoring keeps the build self-contained/offline.)
doctest_dep = declare_dependency(
  include_directories : include_directories('third_party/doctest'))

# wlroots is pinned to 0.19 and version-gated (ABI breaks every minor release).
wlroots_dep        = dependency('wlroots-0.19', version : ['>=0.19.0', '<0.20.0'])
wayland_server_dep = dependency('wayland-server')
wayland_client_dep = dependency('wayland-client')
wayland_protos     = dependency('wayland-protocols')
pixman_dep         = dependency('pixman-1')
libdrm_dep         = dependency('libdrm')
# xkbcommon: M3 creates keyboards and xkb keymaps directly (wlr_keyboard.h pulls
# <xkbcommon/xkbcommon.h>; the seat/keyboard path links libxkbcommon).
xkbcommon_dep      = dependency('xkbcommon')
# fcft (font rasterization, used by foot) drives M3 titlebar text. We call pixman
# directly on its glyph masks, so pixman is linked explicitly alongside it.
fcft_dep           = dependency('fcft', version : '>=3.0.0')

# Generated protocol code: server headers the wlroots headers need (e.g.
# xdg-shell-protocol.h) + client stubs for the in-process test client.
subdir('protocols')

# Two system C headers use constructs that are invalid C++ even inside
# extern "C" — wlroots' wlr_scene.h ('[static 4]') and fcft.h ('[static count]'
# / '[static len]' / 'restrict'). tools/sanitize-c-header.sh strips both; each
# header gets a sanitized copy on the consumer's include path.
sanitize_c_header = find_program('tools/sanitize-c-header.sh')

# wlr_scene_compat: toolkit/wlr.hpp includes it ahead of the system header (it
# shares the include guard, suppressing the original). See toolkit/wlr.hpp.
wlr_incdir = wlroots_dep.get_variable(pkgconfig : 'includedir')
wlr_scene_compat = custom_target('wlr_scene_compat.h',
  output  : 'wlr_scene_compat.h',
  command : [sanitize_c_header,
             wlr_incdir / 'wlroots-0.19' / 'wlr' / 'types' / 'wlr_scene.h'],
  capture : true)

# fcft_compat: fcft.h uses '#pragma once' (not an include guard), so the copy is
# given a distinct name and is the ONLY fcft header anyone includes (nothing else
# in the tree pulls <fcft/fcft.h>); toolkit/text.hpp includes <fcft_compat.h>.
fcft_incdir = fcft_dep.get_variable(pkgconfig : 'includedir')
fcft_compat = custom_target('fcft_compat.h',
  output  : 'fcft_compat.h',
  command : [sanitize_c_header, fcft_incdir / 'fcft' / 'fcft.h'],
  capture : true)

# Combined dependency for everything that includes toolkit/text.hpp: fcft (which
# pulls freetype/harfbuzz/fontconfig) + pixman (we touch glyph masks directly) +
# the sanitized fcft header on the include path.
text_dep = declare_dependency(
  dependencies : [fcft_dep, pixman_dep],
  sources      : fcft_compat)

# One combined dependency for everything that includes toolkit/wlr.hpp: the
# wlroots stack plus the generated compat header (listing it in `sources` puts
# its build dir on the consumer's include path — the same idiom wlroots
# projects use for wayland-scanner output).
wlr_dep = declare_dependency(
  dependencies : [wlroots_dep, wayland_server_dep, pixman_dep, libdrm_dep,
                  xkbcommon_dep, server_protos_dep],
  sources      : wlr_scene_compat)

subdir('toolkit')
subdir('src')
if get_option('tests')
  subdir('tests')
endif
