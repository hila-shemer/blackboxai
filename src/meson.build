# The compositor shell. Links the pure bt:: toolkit plus the wlroots stack
# (wlr_dep also carries the generated wlr_scene_compat.h include path).
src_inc = include_directories('.')
bbai_lib = static_library('bbai',
  files(
    'ClipboardImage.cc',
    'DataBuffer.cc',
    'Screenshot.cc',
    'Server.cc',
    'Output.cc',
    'View.cc',
    'Decoration.cc',
    'StackingList.cc',
    'Toolbar.cc',
    'Keybindings.cc',
    'Keyboard.cc',
    'CommandRunner.cc',
    'Menu.cc',
    'Rootmenu.cc',
    'Workspace.cc',
    'Timer.cc',
  ),
  include_directories : [src_inc],
  dependencies : [bt_dep, bttext_dep, wlr_dep, dependency('libpng')])

bbai_dep = declare_dependency(link_with : bbai_lib,
  include_directories : [src_inc, bt_inc],
  dependencies : [bt_dep, bttext_dep, wlr_dep, dependency('libpng')])

executable('blackboxai', files('main.cc'),
  dependencies : [bbai_dep], install : true)
