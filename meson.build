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

# Generated protocol code: server headers the wlroots headers need (e.g.
# xdg-shell-protocol.h) + client stubs for the in-process test client.
subdir('protocols')

# wlroots 0.19's wlr_scene.h uses the C99 '[static 4]' array-parameter hint,
# which is invalid C++. Generate a sanitized copy (hint -> '[]') that
# toolkit/wlr.hpp includes ahead of the system header (it shares the include
# guard, suppressing the original). See toolkit/wlr.hpp for the full rationale.
wlr_incdir = wlroots_dep.get_variable(pkgconfig : 'includedir')
sanitize_wlr_scene = find_program('tools/sanitize-wlr-scene.sh')
wlr_scene_compat = custom_target('wlr_scene_compat.h',
  output  : 'wlr_scene_compat.h',
  command : [sanitize_wlr_scene,
             wlr_incdir / 'wlroots-0.19' / 'wlr' / 'types' / 'wlr_scene.h'],
  capture : true)

# One combined dependency for everything that includes toolkit/wlr.hpp: the
# wlroots stack plus the generated compat header (listing it in `sources` puts
# its build dir on the consumer's include path — the same idiom wlroots
# projects use for wayland-scanner output).
wlr_dep = declare_dependency(
  dependencies : [wlroots_dep, wayland_server_dep, pixman_dep, libdrm_dep,
                  server_protos_dep],
  sources      : wlr_scene_compat)

subdir('toolkit')
subdir('src')
if get_option('tests')
  subdir('tests')
endif
