# Unit suite — one TU (smoke_test.cc) provides doctest's main; the rest only
# register TEST_CASEs. listener_test.cc pulls in wlr.hpp, so the suite links
# wlroots + wayland-server.
unit_sources = files(
  'unit/smoke_test.cc',
  'unit/color_test.cc',
  'unit/resource_test.cc',
  'unit/texture_test.cc',
  'unit/image_test.cc',
  'unit/image_render_test.cc',
  'unit/listener_test.cc',
  'unit/databuffer_test.cc',
  'unit/text_boundary_test.cc',
  'unit/wlr_headers_test.cc',
  'unit/frame_geometry_test.cc',
  'unit/stackinglist_test.cc',
  'unit/workspace_test.cc',
  'unit/clock_test.cc',
  'unit/timer_test.cc',
  'unit/toolbar_geom_test.cc',
  'unit/keycode_test.cc',
  'unit/keybinding_test.cc',
  'unit/menu_geometry_test.cc',
)
unit_exe = executable('unit-tests', unit_sources,
  dependencies : [doctest_dep, bt_dep, wlr_dep, bbai_dep, text_dep])
test('unit', unit_exe, suite : 'unit')

# Isolated fontconfig: tests that render title text point FONTCONFIG_FILE here
# and run with cwd = source root, so the conf's prefix="cwd" <dir> (holding only
# the bundled LiberationMono) resolves every family to that one font.
fontconfig_conf = meson.project_source_root() / 'tests' / 'fixtures' / 'fonts.conf'

# Text renderer determinism test — its own executable so it can run under the
# isolated fontconfig.
text_test_exe = executable('text-test', files('unit/text_test.cc'),
  dependencies : [doctest_dep, bttext_dep])
test('text', text_test_exe, suite : 'unit',
  workdir : meson.project_source_root(),
  env : ['FONTCONFIG_FILE=' + fontconfig_conf])

# Reusable headless capture + golden harness, plus the in-process test client.
# HeadlessFixture.cc is server-side (bbai_dep) and TestClient.cc is client-side
# (wayland-client + generated client stubs); they never share a TU.
png_dep = dependency('libpng')
harness_lib = static_library('harness',
  files('harness/HeadlessFixture.cc', 'harness/TestClient.cc'),
  dependencies : [bbai_dep, png_dep, client_protos_dep, wayland_client_dep],
  include_directories : include_directories('harness'))
harness_dep = declare_dependency(link_with : harness_lib,
  include_directories : include_directories('harness'),
  dependencies : [bbai_dep, png_dep, client_protos_dep, wayland_client_dep])

# System (L1/L2/L3) suite.
test_env = ['WLR_BACKENDS=headless', 'WLR_RENDERER=pixman']
# Tests whose captured frame contains a decorated window (title text) add the
# isolated fontconfig so the rendered glyphs are host-independent.
text_env = test_env + ['FONTCONFIG_FILE=' + fontconfig_conf]

system_exe = executable('system-tests', files('system/background_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('background', system_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)

client_exe = executable('client-test', files('system/client_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('client', client_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# M3 decoration frame: a mapped SSD client gets a pixel-accurate Blackbox frame.
frame_exe = executable('frame-test', files('system/frame_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('frame', frame_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# M3 decoration policy: request SSD by default, honor a CSD holdout.
decoration_exe = executable('decoration-test', files('system/decoration_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('decoration', decoration_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# M3 input: scene hit-test maps a cursor position to a View + frame part.
hittest_exe = executable('hittest-test', files('system/hittest_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('hittest', hittest_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# M3 interactive move: titlebar drag.
move_exe = executable('move-test', files('system/move_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('move', move_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# M3 interactive resize: grip drag, decoration re-layout on commit.
resize_exe = executable('resize-test', files('system/resize_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('resize', resize_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# M4 clock/timer seam: deterministic virtual clock + timer registry.
clock_seam_exe = executable('clock-seam-test', files('system/clock_seam_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('clock_seam', clock_seam_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)

# M4 toolbar: bottom-center chrome + ticking clock (two goldens).
toolbar_exe = executable('toolbar-test', files('system/toolbar_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('toolbar', toolbar_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# M4 keybindings: Mod4 actions via the deviceless key injector.
keybinding_exe = executable('keybinding-test', files('system/keybinding_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('keybinding', keybinding_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)

# M4 restacking: View is a StackEntity; raise/lower + show/hide.
restack_exe = executable('restack-test', files('system/restack_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('restack', restack_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# M4 workspace switching: show/hide + focus restore + label, two clients.
ws_switch_exe = executable('workspace-switch-test', files('system/workspace_switch_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('workspace_switch', ws_switch_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)
