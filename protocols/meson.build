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
