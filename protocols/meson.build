# Client-side protocol stubs for the in-process test client (xdg-shell +
# single-pixel-buffer). The compositor itself uses wlroots' built-in server
# protocol implementations, so only the client half is generated here.
fs = import('fs')
wl_scanner = find_program('wayland-scanner')
protos_dir = wayland_protos.get_variable(pkgconfig : 'pkgdatadir')

client_protocols = [
  protos_dir / 'stable' / 'xdg-shell' / 'xdg-shell.xml',
  protos_dir / 'staging' / 'single-pixel-buffer' / 'single-pixel-buffer-v1.xml',
]

client_proto_targets = []
foreach xml : client_protocols
  base = fs.stem(xml)
  client_proto_targets += custom_target(base + '-client-header',
    output  : base + '-client-protocol.h',
    command : [wl_scanner, 'client-header', xml, '@OUTPUT@'])
  client_proto_targets += custom_target(base + '-private-code',
    output  : base + '-protocol.c',
    command : [wl_scanner, 'private-code', xml, '@OUTPUT@'])
endforeach

# Listing the generated sources puts their build dir on the consumer's include
# path (same idiom as the wlr_scene_compat shim).
client_protos_dep = declare_dependency(
  sources      : client_proto_targets,
  dependencies : wayland_client_dep)

# Server-side protocol headers required by the wlroots headers themselves
# (wlr_xdg_shell.h includes "xdg-shell-protocol.h"). Folded into wlr_dep so
# every TU that includes toolkit/wlr.hpp can find them.
server_protocols = [
  protos_dir / 'stable' / 'xdg-shell' / 'xdg-shell.xml',
]
server_proto_targets = []
foreach xml : server_protocols
  base = fs.stem(xml)
  server_proto_targets += custom_target(base + '-server-header',
    output  : base + '-protocol.h',
    command : [wl_scanner, 'server-header', xml, '@OUTPUT@'])
endforeach

server_protos_dep = declare_dependency(sources : server_proto_targets)
