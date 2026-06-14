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
)
unit_exe = executable('unit-tests', unit_sources,
  dependencies : [doctest_dep, bt_dep, wlr_dep, bbai_dep, text_dep])
test('unit', unit_exe, suite : 'unit')

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

system_exe = executable('system-tests', files('system/background_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('background', system_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)

client_exe = executable('client-test', files('system/client_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('client', client_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)
