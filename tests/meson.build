# Unit suite — one TU (smoke_test.cc) provides doctest's main; the rest only
# register TEST_CASEs. listener_test.cc pulls in wlr.hpp, so the suite links
# wlroots + wayland-server.
unit_sources = files(
  'unit/smoke_test.cc',
  'unit/color_test.cc',
  'unit/resource_test.cc',
  'unit/config_test.cc',
  'unit/texture_test.cc',
  'unit/image_test.cc',
  'unit/image_render_test.cc',
  'unit/listener_test.cc',
  'unit/databuffer_test.cc',
  'unit/text_boundary_test.cc',
  'unit/wlr_headers_test.cc',
  'unit/frame_geometry_test.cc',
  'unit/stackinglist_test.cc',
  'unit/mru_test.cc',
  'unit/workspace_test.cc',
  'unit/clock_test.cc',
  'unit/timer_test.cc',
  'unit/toolbar_geom_test.cc',
  'unit/screenshot_geom_test.cc',
  'unit/keycode_test.cc',
  'unit/keybinding_test.cc',
  'unit/menu_geometry_test.cc',
  'unit/command_runner_test.cc',
  'unit/rootmenu_submenu_test.cc',
  'unit/autostart_test.cc',
  'unit/desktop_file_test.cc',
  'unit/server_ok_test.cc',
  'unit/menu_parser_test.cc',
  'unit/sni_model_test.cc',
  'unit/work_area_geom_test.cc',
  'unit/util_test.cc',
  'unit/rootmenu_wire_test.cc',
  'unit/slit_geom_test.cc',
)
unit_exe = executable('unit-tests', unit_sources,
  cpp_args : ['-DBBAI_SESSION_DESKTOP="' +
              meson.project_source_root() / 'data' / 'blackboxai.desktop' + '"',
              # config_test.cc loads a .blackboxrc fixture by absolute path.
              '-DBBAI_TEST_FIXTURE_DIR="'
              + meson.project_source_root() / 'tests' / 'fixtures' + '"',
              '-DBBAI_MENU_FIXTURE="@0@"'.format(
                meson.current_source_dir() / 'fixtures' / 'menu.in')],
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

# Style loader policy + metric formulas - own executable so it runs under the
# isolated fontconfig (Style constructs TextRenderers) with workdir = source
# root (it loads data/styles/* by relative path).
style_test_exe = executable('style-test', files('unit/style_test.cc'),
  dependencies : [doctest_dep, bt_dep, bbai_dep, text_dep])
test('style', style_test_exe, suite : 'unit',
  workdir : meson.project_source_root(),
  env : ['FONTCONFIG_FILE=' + fontconfig_conf])

# Reusable headless capture + golden harness, plus the in-process test client.
# HeadlessFixture.cc is server-side (bbai_dep) and TestClient.cc is client-side
# (wayland-client + generated client stubs); they never share a TU.
png_dep = dependency('libpng')
harness_lib = static_library('harness',
  files('harness/HeadlessFixture.cc', 'harness/TestClient.cc',
        'harness/LockTestClient.cc'),
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
# text_env, not test_env: since M4 the toolbar (clock + workspace label) is in every
# captured frame, so this golden renders glyphs too - it just predates the rule.
test('background', system_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# rc/style boot path: -rc -> Config -> style ladder -> styled desktop.
style_boot_exe = executable('style-boot-test', files('system/style_boot_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('style_boot', style_boot_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# Gray style end-to-end: wildcard keys + parentrelative label pixel-copy.
gray_window_exe = executable('gray-window-test', files('system/gray_window_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('gray_window', gray_window_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# Toolbar knobs from the rc (placement/width/autoHide/enable) - config-driven,
# not ForTest-driven.
toolbar_config_exe = executable('toolbar-config-test', files('system/toolbar_config_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('toolbar_config', toolbar_config_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# Root menu rendered under the Results style (bordered textures, two fonts).
styled_menu_exe = executable('styled-menu-test', files('system/styled_menu_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('styled_menu', styled_menu_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# rc-driven startup state: workspace count/names (focusNewWindows joins in
# the next task).
rc_boot_exe = executable('rc-boot-test', files('system/rc_boot_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('rc_boot', rc_boot_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# Live re-theme: reconfigure()/applyStyleFile() - the wave-2 configmenu seams.
retheme_exe = executable('retheme-test', files('system/retheme_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('retheme', retheme_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

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

# MRU alt-tab: cycle session state machine via injected seams (focus + MRU order,
# preview vs commit, wraparound, cancel, cross-workspace commit). Nonvisual.
alttab_exe = executable('alttab-test', files('system/alttab_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('alttab', alttab_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)

# M4 root menu: modal chrome on overlay + dismiss + actions.
rootmenu_exe = executable('rootmenu-test', files('system/rootmenu_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('rootmenu', rootmenu_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)
menu_action_exe = executable('menu-action-test', files('system/menu_action_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('menu_action', menu_action_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# F3.3 submenu hover-open: hovering "Workspaces" opens the child menu to the right.
rootmenu_submenu_exe = executable('rootmenu-submenu-test',
  files('system/rootmenu_submenu_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('rootmenu_submenu', rootmenu_submenu_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# M5 menu-wire: file-driven root menu - tree + goldens, stat-on-open reload,
# include end-to-end, fallback; action dispatch joins in the next task.
menu_file_exe = executable('menu-file-test', files('system/menu_file_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('menu_file', menu_file_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# F4.2 View iconified state: composes with workspace visibility (nonvisual, no golden).
iconify_exe = executable('iconify-test', files('system/iconify_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('iconify', iconify_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)

# F4.4 Close button: press+release-inside sends xdg close request (nonvisual, no golden).
close_button_exe = executable('close-button-test', files('system/close_button_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('close_button', close_button_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# Region screenshot: in-memory PNG encoder round-trip.
screenshot_png_exe = executable('screenshot-png-test',
  files('system/screenshot_png_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('screenshot_png', screenshot_png_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)

# Region screenshot: scene readback crop (texture_from_buffer + read_pixels).
screenshot_capture_exe = executable('screenshot-capture-test',
  files('system/screenshot_capture_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('screenshot_capture', screenshot_capture_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# Region screenshot: server-side image/png data source + async writer.
clipboard_image_exe = executable('clipboard-image-test',
  files('system/clipboard_image_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('clipboard_image', clipboard_image_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)

# Region screenshot: ScreenshotSelect state machine + dim overlay + cancel paths.
screenshot_select_exe = executable('screenshot-select-test',
  files('system/screenshot_select_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('screenshot_select', screenshot_select_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# Region screenshot: end-to-end release -> capture -> clipboard.
screenshot_e2e_exe = executable('screenshot-e2e-test',
  files('system/screenshot_e2e_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('screenshot_e2e', screenshot_e2e_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# F1.2 View focus state: carry + relayout guard (nonvisual, no golden).
view_focus_exe = executable('view-focus-test', files('system/view_focus_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('view_focus', view_focus_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)

# F4.5 Maximize button: press+release-inside toggles maximize with save/restore (nonvisual, no golden).
maximize_exe = executable('maximize-test', files('system/maximize_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('maximize', maximize_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)

# F4.6 Maximized golden: a maximized SSD window fills the work area above the toolbar.
maximized_golden_exe = executable('maximized-golden-test',
  files('system/maximized_golden_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('maximized_golden', maximized_golden_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# F1.3 focusView/clearFocus restyle: titlebar click propagates focus state to both views.
focus_restyle_exe = executable('focus-restyle-test', files('system/focus_restyle_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('focus_restyle', focus_restyle_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)

# F1.4 focus-swap golden: two SSD windows, focused vs unfocused titlebars differ.
focus_swap_exe = executable('focus-swap-test', files('system/focus_swap_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('focus_swap', focus_swap_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# F4.7 Icon menu: middle-click / Mod4+Alt+T opens icon list; click deiconifies.
icon_menu_exe = executable('icon-menu-test', files('system/icon_menu_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('icon_menu', icon_menu_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# F2.3 Toolbar TopCenter placement golden.
toolbar_placement_exe = executable('toolbar-placement-test',
  files('system/toolbar_placement_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('toolbar_placement', toolbar_placement_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# F2.5 Toolbar auto-hide: state + hide timer (position only, no golden).
toolbar_autohide_exe = executable('toolbar-autohide-test',
  files('system/toolbar_autohide_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('toolbar_autohide', toolbar_autohide_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)

# F2.6 Toolbar auto-hide hidden golden + reveal via pointer motion.
toolbar_autohide_golden_exe = executable('toolbar-autohide-golden-test',
  files('system/toolbar_autohide_golden_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('toolbar_autohide_golden', toolbar_autohide_golden_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# M7 multi-output: every head becomes an Output; toolbar stays primary (nonvisual).
multi_output_exe = executable('multi-output-test', files('system/multi_output_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('multi_output', multi_output_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)

# Work-area slice: per-output fullBox/workArea + strut registry + resolution.
work_area_exe = executable('work-area-test', files('system/work_area_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('work_area', work_area_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)

# Work-area slice: hot-unplug hygiene (best-effort headless destroy drive).
output_destroy_exe = executable('output-destroy-test',
  files('system/output_destroy_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('output_destroy', output_destroy_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)

# M7 autostart wiring: glob + filter + spawn .desktop entries via the fake runner.
autostart_wiring_exe = executable('autostart-wiring-test',
  files('system/autostart_wiring_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('autostart_wiring', autostart_wiring_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)

# lock-idle: ext-session-lock-v1 lifecycle (blank, lock surface, unlock, crash).
session_lock_exe = executable('session-lock-test', files('system/session_lock_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('session_lock', session_lock_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# lock-idle: locking mid-modal-mode aborts the mode (menu/screenshot/cycle/grab).
lock_interactions_exe = executable('lock-interactions-test',
  files('system/lock_interactions_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('lock_interactions', lock_interactions_exe, suite : 'system',
  workdir : meson.project_source_root(), env : text_env)

# Harness self-test: the bounded-retry connect (the plan's recorded cold-cache
# lock_interactions flake remedy) - no compositor, pure client-side.
harness_connect_exe = executable('harness-connect-test',
  files('system/harness_connect_test.cc'),
  include_directories : include_directories('harness'),
  dependencies : [doctest_dep, wayland_client_dep, dependency('threads')])
test('harness_connect', harness_connect_exe, suite : 'system', env : test_env)

# lock-idle: ext-idle-notify-v1 wiring (timeout=0 = deterministic, no goldens).
idle_notify_exe = executable('idle-notify-test', files('system/idle_notify_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('idle_notify', idle_notify_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)

# ---- Demo scenarios (suite: 'demo') ----------------------------------------
# Each is a normal integration test that asserts multi-step behavior AND, when
# BBAI_DEMO_DIR is set, dumps per-frame PNGs for video encoding via
# tools/make-demos.sh.  The text_env is required because these tests render
# title/menu text.

demo_focus_swap_exe = executable('demo-focus-swap',
  files('system/demo_focus_swap.cc'),
  dependencies : [harness_dep, doctest_dep])
test('demo_focus_swap', demo_focus_swap_exe, suite : 'demo',
  workdir : meson.project_source_root(), env : text_env)

demo_cascade_menu_exe = executable('demo-cascade-menu',
  files('system/demo_cascade_menu.cc'),
  dependencies : [harness_dep, doctest_dep])
test('demo_cascade_menu', demo_cascade_menu_exe, suite : 'demo',
  workdir : meson.project_source_root(), env : text_env)

demo_window_ops_exe = executable('demo-window-ops',
  files('system/demo_window_ops.cc'),
  dependencies : [harness_dep, doctest_dep])
test('demo_window_ops', demo_window_ops_exe, suite : 'demo',
  workdir : meson.project_source_root(), env : text_env)

demo_toolbar_exe = executable('demo-toolbar',
  files('system/demo_toolbar.cc'),
  dependencies : [harness_dep, doctest_dep])
test('demo_toolbar', demo_toolbar_exe, suite : 'demo',
  workdir : meson.project_source_root(), env : text_env)

# ---- sni-core (suite: 'system') ---------------------------------------------
# The tray's D-Bus half. dbus-run-session wraps each exe so every run gets its
# own private bus daemon - claiming org.kde.StatusNotifierWatcher never touches
# the developer's real session bus.
dbus_run_session = find_program('dbus-run-session')
sni_host_exe = executable('sni-host-test',
  files('system/sni_host_test.cc', 'harness/SniMockItem.cc'),
  dependencies : [bbai_dep, doctest_dep, libsystemd_dep],
  include_directories : include_directories('harness'))
test('sni_host', dbus_run_session, args : ['--', sni_host_exe], suite : 'system',
  workdir : meson.project_source_root())

sni_server_exe = executable('sni-server-test',
  files('system/sni_server_test.cc', 'harness/SniMockItem.cc'),
  dependencies : [bbai_dep, doctest_dep, libsystemd_dep],
  include_directories : include_directories('harness'))
test('sni_server', dbus_run_session, args : ['--', sni_server_exe], suite : 'system',
  workdir : meson.project_source_root(), env : test_env)
