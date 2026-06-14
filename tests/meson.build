unit_sources = files('unit/smoke_test.cc')
unit_exe = executable('unit-tests', unit_sources,
  dependencies : [doctest_dep, bt_dep])
test('unit', unit_exe, suite : 'unit')
