# Unit suite — one TU (smoke_test.cc) provides doctest's main; the rest only
# register TEST_CASEs. listener_test.cc pulls in wlr.hpp, so the suite links
# wlroots + wayland-server.
unit_sources = files(
  'unit/smoke_test.cc',
  'unit/color_test.cc',
  'unit/resource_test.cc',
  'unit/texture_test.cc',
  'unit/image_test.cc',
  'unit/listener_test.cc',
)
unit_exe = executable('unit-tests', unit_sources,
  dependencies : [doctest_dep, bt_dep, wlr_dep])
test('unit', unit_exe, suite : 'unit')
