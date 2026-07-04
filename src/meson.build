# The compositor shell. Links the pure bt:: toolkit plus the wlroots stack
# (wlr_dep also carries the generated wlr_scene_compat.h include path).
src_inc = include_directories('.')

# Install-path defaults baked into the lib objects (Config.cc reads them);
# test binaries link the lib, so no test-side defines are needed.
bbai_cpp_args = [
  '-DBBAI_DEFAULT_STYLE="@0@"'.format(
    get_option('prefix') / get_option('datadir') / 'blackboxai' / 'styles' / 'Results'),
  '-DBBAI_DEFAULT_MENU="@0@"'.format(
    get_option('prefix') / get_option('datadir') / 'blackboxai' / 'menu'),
]

bbai_lib = static_library('bbai',
  files(
    'ClipboardImage.cc',
    'Config.cc',
    'Style.cc',
    'DataBuffer.cc',
    'Screenshot.cc',
    'Server.cc',
    'SessionLock.cc',
    'Output.cc',
    'Autostart.cc',
    'View.cc',
    'Decoration.cc',
    'StackingList.cc',
    'Toolbar.cc',
    'Slit.cc',
    'Keybindings.cc',
    'Keyboard.cc',
    'CommandRunner.cc',
    'Menu.cc',
    'MenuParser.cc',
    'Rootmenu.cc',
    'Windowmenu.cc',
    'Barmenu.cc',
    'SniMenu.cc',
    'Workspace.cc',
    'Timer.cc',
    'SniHost.cc',
  ),
  include_directories : [src_inc],
  cpp_args : bbai_cpp_args,
  dependencies : [bt_dep, bttext_dep, wlr_dep, libsystemd_dep, dependency('libpng')])

bbai_dep = declare_dependency(link_with : bbai_lib,
  include_directories : [src_inc, bt_inc],
  dependencies : [bt_dep, bttext_dep, wlr_dep, libsystemd_dep, dependency('libpng')])

executable('blackboxai', files('main.cc'),
  dependencies : [bbai_dep], install : true)
