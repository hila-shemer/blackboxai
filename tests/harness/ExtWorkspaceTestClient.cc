#include "ExtWorkspaceTestClient.hh"
#include "ConnectRetry.hh"

#include <wayland-client.h>
#include "ext-workspace-v1-client-protocol.h"

#include <cstdint>
#include <cstring>
#include <poll.h>
#include <vector>

namespace bbai::test {

  namespace {
    void pumpDisplay(wl_display *d) {   // mirrors LockTestClient::pumpDisplay
      while (wl_display_prepare_read(d) != 0) wl_display_dispatch_pending(d);
      wl_display_flush(d);
      pollfd pfd = { wl_display_get_fd(d), POLLIN, 0 };
      if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) wl_display_read_events(d);
      else wl_display_cancel_read(d);
      wl_display_dispatch_pending(d);
    }
  }

  struct WsState {
    ext_workspace_handle_v1 *handle = nullptr;
    std::string name;
    bool active = false;
    bool removed = false;
  };

  struct ExtWorkspaceTestClient::Impl {
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    ext_workspace_manager_v1 *manager = nullptr;
    std::vector<WsState *> workspaces;   // creation order == our model index order

    WsState *find(ext_workspace_handle_v1 *h) {
      for (auto *w : workspaces) if (w->handle == h) return w;
      return nullptr;
    }
    // Live (non-removed) list in stable order.
    std::vector<WsState *> live() const {
      std::vector<WsState *> v;
      for (auto *w : workspaces) if (!w->removed) v.push_back(w);
      return v;
    }
  };

  // ---- workspace handle events ----
  static void ws_id(void *, ext_workspace_handle_v1 *, const char *) {}
  static void ws_name(void *data, ext_workspace_handle_v1 *, const char *name) {
    static_cast<WsState *>(data)->name = name ? name : "";
  }
  static void ws_coordinates(void *, ext_workspace_handle_v1 *, wl_array *) {}
  // The generated `state` event carries a uint32_t bitmask (the plan's prose
  // guessed a wl_array; the container header is the source of truth).
  static void ws_state(void *data, ext_workspace_handle_v1 *, uint32_t state) {
    static_cast<WsState *>(data)->active =
        (state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE) != 0;
  }
  static void ws_capabilities(void *, ext_workspace_handle_v1 *, uint32_t) {}
  static void ws_removed(void *data, ext_workspace_handle_v1 *) {
    static_cast<WsState *>(data)->removed = true;
  }
  static const ext_workspace_handle_v1_listener s_ws_listener = {
    ws_id, ws_name, ws_coordinates, ws_state, ws_capabilities, ws_removed };

  // ---- manager events ----
  // NOTE member order: the generated ext_workspace_manager_v1_listener is
  // { workspace_group, workspace, done, finished } - the group callback comes
  // FIRST (the plan's prose had these two swapped; the container header is the
  // source of truth).
  static void mgr_group(void *, ext_workspace_manager_v1 *,
                        ext_workspace_group_handle_v1 *) {}   // one group; ignored
  static void mgr_workspace(void *data, ext_workspace_manager_v1 *,
                            ext_workspace_handle_v1 *handle) {
    auto *impl = static_cast<ExtWorkspaceTestClient::Impl *>(data);
    auto *w = new WsState();
    w->handle = handle;
    impl->workspaces.push_back(w);
    ext_workspace_handle_v1_add_listener(handle, &s_ws_listener, w);
  }
  static void mgr_done(void *, ext_workspace_manager_v1 *) {}
  static void mgr_finished(void *, ext_workspace_manager_v1 *) {}
  static const ext_workspace_manager_v1_listener s_mgr_listener = {
    mgr_group, mgr_workspace, mgr_done, mgr_finished };

  static void reg_global(void *data, wl_registry *reg, uint32_t name,
                         const char *iface, uint32_t) {
    auto *impl = static_cast<ExtWorkspaceTestClient::Impl *>(data);
    if (std::strcmp(iface, ext_workspace_manager_v1_interface.name) == 0) {
      impl->manager = static_cast<ext_workspace_manager_v1 *>(
        wl_registry_bind(reg, name, &ext_workspace_manager_v1_interface, 1));
      ext_workspace_manager_v1_add_listener(impl->manager, &s_mgr_listener, impl);
    }
  }
  static void reg_remove(void *, wl_registry *, uint32_t) {}
  static const wl_registry_listener s_reg_listener = { reg_global, reg_remove };

  ExtWorkspaceTestClient::ExtWorkspaceTestClient(const std::string &socket) {
    impl = new Impl();
    impl->display = connectWithRetry(socket.c_str());
    if (!impl->display) return;
    impl->registry = wl_display_get_registry(impl->display);
    wl_registry_add_listener(impl->registry, &s_reg_listener, impl);
    wl_display_flush(impl->display);
  }
  ExtWorkspaceTestClient::~ExtWorkspaceTestClient() {
    if (impl->display) {
      for (auto *w : impl->workspaces) {
        if (w->handle) ext_workspace_handle_v1_destroy(w->handle);
        delete w;
      }
      if (impl->manager) ext_workspace_manager_v1_destroy(impl->manager);
      wl_display_flush(impl->display);
      wl_display_disconnect(impl->display);
    }
    delete impl;
  }

  bool ExtWorkspaceTestClient::ok() const { return impl && impl->display; }
  void ExtWorkspaceTestClient::flush() { if (impl->display) wl_display_flush(impl->display); }
  void ExtWorkspaceTestClient::pump() { if (impl->display) pumpDisplay(impl->display); }

  bool ExtWorkspaceTestClient::sawManager() const { return impl->manager != nullptr; }
  int ExtWorkspaceTestClient::workspaceCount() const {
    return static_cast<int>(impl->live().size());
  }
  std::string ExtWorkspaceTestClient::name(int i) const {
    auto v = impl->live();
    return (i >= 0 && i < static_cast<int>(v.size())) ? v[i]->name : "";
  }
  int ExtWorkspaceTestClient::activeIndex() const {
    auto v = impl->live();
    for (int i = 0; i < static_cast<int>(v.size()); ++i) if (v[i]->active) return i;
    return -1;
  }

  // -- grown in Task 3 --
  void ExtWorkspaceTestClient::activate(int i) {
    auto v = impl->live();
    if (i < 0 || i >= static_cast<int>(v.size())) return;
    ext_workspace_handle_v1_activate(v[i]->handle);
    ext_workspace_manager_v1_commit(impl->manager);
    wl_display_flush(impl->display);
  }

} // namespace bbai::test
