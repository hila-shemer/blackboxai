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

# bt::TextRenderer is split into its own lib so the pure bt_lib stays free of the
# fcft/pixman link; only consumers that draw text (decorations, the text test)
# pull bttext_dep. text_dep is declared in the top-level meson.build.
bttext_lib = static_library('bttext',
  files('Text.cc'),
  include_directories : bt_inc,
  dependencies : [bt_dep, text_dep])
bttext_dep = declare_dependency(link_with : bttext_lib,
  include_directories : bt_inc,
  dependencies : [bt_dep, text_dep])
