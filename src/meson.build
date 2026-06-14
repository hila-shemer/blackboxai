# The compositor shell. Links the pure bt:: toolkit plus the wlroots stack
# (wlr_dep also carries the generated wlr_scene_compat.h include path).
src_inc = include_directories('.')
bbai_lib = static_library('bbai',
  files(
    'DataBuffer.cc',
    'Server.cc',
    'Output.cc',
    'View.cc',
    'Decoration.cc',
    'StackingList.cc',
    'Workspace.cc',
  ),
  include_directories : [src_inc],
  dependencies : [bt_dep, bttext_dep, wlr_dep])

bbai_dep = declare_dependency(link_with : bbai_lib,
  include_directories : [src_inc, bt_inc],
  dependencies : [bt_dep, bttext_dep, wlr_dep])

executable('blackboxai', files('main.cc'),
  dependencies : [bbai_dep], install : true)
