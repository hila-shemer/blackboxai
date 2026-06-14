# The ported bt:: toolkit — pure CPU code (no wlroots/X link). The boundary
# headers wlr.hpp/listener.hpp live here too but are header-only (consumers add
# wlroots_dep themselves).
bt_inc = include_directories('.')
bt_lib = static_library('bt',
  files(
    'Color.cc',
    'Resource.cc',
    'Texture.cc',
    'Image.cc',
  ),
  include_directories : bt_inc)
bt_dep = declare_dependency(link_with : bt_lib, include_directories : bt_inc)
