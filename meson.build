project('blackboxai', 'cpp',
  version : '0.1.0',
  default_options : ['cpp_std=c++20', 'warning_level=3', 'werror=false'])

add_project_arguments('-DWLR_USE_UNSTABLE', language : 'cpp')

cc = meson.get_compiler('cpp')

# Vendored single-header unit framework: third_party/doctest/doctest/doctest.h
# (Fedora has no doctest-devel; vendoring keeps the build self-contained/offline.)
doctest_dep = declare_dependency(
  include_directories : include_directories('third_party/doctest'))

subdir('toolkit')
if get_option('tests')
  subdir('tests')
endif
