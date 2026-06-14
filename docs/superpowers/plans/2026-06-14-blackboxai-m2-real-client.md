# BlackboxAI Milestone 2 — Real client mapped & composited — Plan

**Goal:** A real Wayland client connects, maps an `xdg_toplevel`, and the
compositor composites it (undecorated) over the gradient background — proven by
a headless golden-PNG test where the client paints a known solid color via the
single-pixel-buffer protocol.

**Builds on M1.** Reuses the renderer seam, `Server`/`Output`, the headless
fixture, and the wlr boundary shim unchanged. Adds the client half of the
compositor (xdg-shell, surface mapping) plus an in-process test client.

## Architecture decisions

- **Test client is in-process, single-threaded.** The test boots a headless
  `Server` (which now `wl_display_add_socket_auto`), then a libwayland *client*
  connects to that socket in the same process. The test interleaves
  `wl_display_dispatch_pending`/`flush` on the client with `server.dispatch()`
  until the toplevel maps, then captures. No threads, no subprocess →
  deterministic, GPU-free. (Pattern: wlroots' own headless tests.)
- **Known color via `single-pixel-buffer-v1`.** Avoids shm plumbing; the client
  paints one exact premultiplied color (e.g. opaque red) scaled to the toplevel
  size by the compositor's scene scaling. The server exposes
  `wlr_single_pixel_buffer_manager_v1_create`.
- **No decorations / no input yet.** A `View` parks the toplevel at a *fixed*
  position+size so the golden knows where to look (e.g. 200x150 at (160,120)).
  Move/resize/focus/keyboard are M3+.
- **Protocols codegen** only for the *client* side (xdg-shell + single-pixel
  buffer client stubs via `wayland-scanner`); wlroots provides the server side.

## Verified APIs (wlroots 0.19, this host)
- `wlr_xdg_shell_create(display, version=6)`; `shell->events.new_toplevel`
  → `struct wlr_xdg_toplevel*` (has `.base` xdg_surface, `.base->surface`).
- Surface lifecycle in 0.19 is on `wlr_surface.events` (`map`/`unmap`/`commit`)
  and `xdg_surface->surface`; `xdg_toplevel->base->initial_commit` gates the
  first `wlr_xdg_toplevel_set_size`/configure. VERIFY exact field names against
  `wlr_xdg_shell.h` before coding (Task 2).
- `wlr_scene_xdg_surface_create(parent_tree, xdg_surface)` → `wlr_scene_tree*`.
- `wlr_single_pixel_buffer_manager_v1_create(display)`.
- `wlr_subcompositor_create(display)`, `wlr_data_device_manager_create(display)`.
- Client: `wayland-client` 1.24, `wayland-scanner` present; protocols at
  `/usr/share/wayland-protocols/{stable/xdg-shell,staging/single-pixel-buffer}`.

## Tasks (TDD; each compile+test+commit)
1. **Protocols + meson codegen.** `protocols/meson.build`: wayland-scanner
   `client-header` + `private-code` for `xdg-shell` and `single-pixel-buffer-v1`;
   a `client_protos_dep`. Smoke: a TU including the generated headers compiles.
2. **Server: client globals + socket.** Add `wlr_xdg_shell`, single-pixel-buffer
   manager, subcompositor, data-device; `wl_display_add_socket_auto` (store the
   socket name; expose for tests). Wire `new_toplevel` → `View`. Unit/compile
   gate: server still boots headless (M1 system test stays green).
3. **`src/View.{hh,cc}`.** Wrap `wlr_xdg_toplevel`; on `initial_commit` send a
   fixed configure (200x150); `wlr_scene_xdg_surface_create` into
   `layer_window`; position at (160,120); listeners map/unmap/commit/destroy
   (RAII `bt::Listener`). `Server` keeps a `std::vector<std::unique_ptr<View>>`.
4. **Test client harness** `tests/harness/TestClient.{hh,cc}`: connect to a
   socket name, bind globals, create surface+xdg_surface+xdg_toplevel, ack the
   configure, attach a single-pixel buffer of a given ARGB, commit. Expose
   `pump(Server&)` that interleaves client+server dispatch until mapped.
5. **System golden test** `tests/system/client_test.cc`: boot Server, run the
   client to map a red 200x150 toplevel at (160,120), capture; assert the
   window rect is red (tolerance) and a point outside is the gradient. BLESS
   the golden.
6. **Coverage + CI.** Keep combined ≥80%; add `View`/client paths. Update the
   CI deps if needed (wayland-devel already present).

## Acceptance
- `ninja` builds with the new protocols + View.
- `meson test --suite system` maps a real client headlessly and matches the new
  golden (red window over the gradient); M1 background golden still passes.
- Combined `toolkit/`+`src/` line coverage stays ≥80%.
