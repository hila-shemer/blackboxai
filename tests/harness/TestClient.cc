#include "TestClient.hh"

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"

#include <cstring>
#include <poll.h>
#include <unistd.h>
#include <sys/mman.h>

namespace bbai::test {

  struct TestClient::Impl {
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    wl_compositor *compositor = nullptr;
    xdg_wm_base *wm_base = nullptr;
    wl_shm *shm = nullptr;
    zxdg_decoration_manager_v1 *deco_mgr = nullptr;
    wl_surface *surface = nullptr;
    xdg_surface *xdgsurf = nullptr;
    xdg_toplevel *toplevel = nullptr;
    zxdg_toplevel_decoration_v1 *decoration = nullptr;
    wl_buffer *buffer = nullptr;
    wl_seat *seat = nullptr;
    wl_pointer *pointer = nullptr;
    uint32_t argb = 0;
    int w = 0, h = 0;
    int pending_w = 0, pending_h = 0;  // size from the latest toplevel.configure
    TestClient::Deco deco = TestClient::Deco::None;
    bool created = false;
    bool got_close = false;       // compositor requested xdg_toplevel.close
    int pointer_buttons = 0;      // count of wl_pointer.button events received
  };

  static wl_buffer *makeShmBuffer(TestClient::Impl *c) {
    const int stride = c->w * 4;
    const int size = stride * c->h;
    int fd = memfd_create("bbai-shm", MFD_CLOEXEC);
    if (fd < 0) return nullptr;
    if (ftruncate(fd, size) < 0) { close(fd); return nullptr; }
    void *map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) { close(fd); return nullptr; }
    uint32_t *px = static_cast<uint32_t *>(map);
    for (int i = 0; i < c->w * c->h; ++i) px[i] = c->argb;
    wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd, size);
    wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, c->w, c->h, stride,
                                               WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(map, size);
    close(fd);
    return buf;
  }

  // xdg_wm_base ping/pong (keeps the client responsive).
  static void wm_ping(void *, xdg_wm_base *b, uint32_t serial) {
    xdg_wm_base_pong(b, serial);
  }
  static const xdg_wm_base_listener s_wm_base_listener = { wm_ping };

  // xdg_toplevel.configure carries the size the compositor wants; remember it so
  // the next surface.configure can (re)make a buffer of that size (resize).
  static void tl_configure(void *data, xdg_toplevel *, int32_t width,
                           int32_t height, wl_array *) {
    auto *c = static_cast<TestClient::Impl *>(data);
    if (width > 0 && height > 0) { c->pending_w = width; c->pending_h = height; }
  }
  static void tl_close(void *data, xdg_toplevel *) {
    static_cast<TestClient::Impl *>(data)->got_close = true;
  }
  static const xdg_toplevel_listener s_xdg_toplevel_listener = {
    .configure = tl_configure, .close = tl_close };

  // xdg_surface.configure: ack and (re)attach the colored buffer at the size the
  // compositor asked for (creating it on first map, replacing it on resize).
  static void surf_configure(void *data, xdg_surface *xs, uint32_t serial) {
    auto *c = static_cast<TestClient::Impl *>(data);
    xdg_surface_ack_configure(xs, serial);
    const int tw = c->pending_w > 0 ? c->pending_w : c->w;
    const int th = c->pending_h > 0 ? c->pending_h : c->h;
    if (!c->buffer || tw != c->w || th != c->h) {
      if (c->buffer) wl_buffer_destroy(c->buffer);
      c->w = tw;
      c->h = th;
      c->buffer = makeShmBuffer(c);
      wl_surface_attach(c->surface, c->buffer, 0, 0);
      wl_surface_damage_buffer(c->surface, 0, 0, c->w, c->h);
      wl_surface_commit(c->surface);
    }
  }
  static const xdg_surface_listener s_xdg_surface_listener = { surf_configure };

  static void createToplevel(TestClient::Impl *c) {
    c->surface = wl_compositor_create_surface(c->compositor);
    c->xdgsurf = xdg_wm_base_get_xdg_surface(c->wm_base, c->surface);
    xdg_surface_add_listener(c->xdgsurf, &s_xdg_surface_listener, c);
    c->toplevel = xdg_surface_get_toplevel(c->xdgsurf);
    xdg_toplevel_add_listener(c->toplevel, &s_xdg_toplevel_listener, c);
    xdg_toplevel_set_title(c->toplevel, "bbai-test");
    if (c->deco != TestClient::Deco::None && c->deco_mgr) {
      c->decoration =
        zxdg_decoration_manager_v1_get_toplevel_decoration(c->deco_mgr, c->toplevel);
      zxdg_toplevel_decoration_v1_set_mode(
        c->decoration, c->deco == TestClient::Deco::RequestSSD
                         ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
                         : ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
    }
    wl_surface_commit(c->surface);  // no buffer yet -> drives the initial configure
  }

  // wl_pointer: a v1 client, so only enter/leave/motion/button/axis can arrive.
  // We only care about button events (to prove the client gets NO orphan release
  // after a modal-menu dismissal); the rest are no-ops so libwayland never calls
  // a null listener slot.
  static void ptr_enter(void *, wl_pointer *, uint32_t, wl_surface *, wl_fixed_t, wl_fixed_t) {}
  static void ptr_leave(void *, wl_pointer *, uint32_t, wl_surface *) {}
  static void ptr_motion(void *, wl_pointer *, uint32_t, wl_fixed_t, wl_fixed_t) {}
  static void ptr_button(void *data, wl_pointer *, uint32_t, uint32_t, uint32_t, uint32_t) {
    static_cast<TestClient::Impl *>(data)->pointer_buttons++;
  }
  static void ptr_axis(void *, wl_pointer *, uint32_t, uint32_t, wl_fixed_t) {}
  static const wl_pointer_listener s_pointer_listener = {
    .enter = ptr_enter, .leave = ptr_leave, .motion = ptr_motion,
    .button = ptr_button, .axis = ptr_axis };

  static void seat_caps(void *data, wl_seat *seat, uint32_t caps) {
    auto *c = static_cast<TestClient::Impl *>(data);
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !c->pointer) {
      c->pointer = wl_seat_get_pointer(seat);
      wl_pointer_add_listener(c->pointer, &s_pointer_listener, c);
    }
  }
  static void seat_name(void *, wl_seat *, const char *) {}
  static const wl_seat_listener s_seat_listener = { .capabilities = seat_caps, .name = seat_name };

  static void reg_global(void *data, wl_registry *reg, uint32_t name,
                         const char *iface, uint32_t) {
    auto *c = static_cast<TestClient::Impl *>(data);
    if (std::strcmp(iface, wl_seat_interface.name) == 0) {
      c->seat = static_cast<wl_seat *>(
        wl_registry_bind(reg, name, &wl_seat_interface, 1));
      wl_seat_add_listener(c->seat, &s_seat_listener, c);
    } else if (std::strcmp(iface, wl_compositor_interface.name) == 0) {
      c->compositor = static_cast<wl_compositor *>(
        wl_registry_bind(reg, name, &wl_compositor_interface, 4));
    } else if (std::strcmp(iface, xdg_wm_base_interface.name) == 0) {
      c->wm_base = static_cast<xdg_wm_base *>(
        wl_registry_bind(reg, name, &xdg_wm_base_interface, 1));
      xdg_wm_base_add_listener(c->wm_base, &s_wm_base_listener, c);
    } else if (std::strcmp(iface, wl_shm_interface.name) == 0) {
      c->shm = static_cast<wl_shm *>(
        wl_registry_bind(reg, name, &wl_shm_interface, 1));
    } else if (std::strcmp(iface, zxdg_decoration_manager_v1_interface.name) == 0) {
      c->deco_mgr = static_cast<zxdg_decoration_manager_v1 *>(
        wl_registry_bind(reg, name, &zxdg_decoration_manager_v1_interface, 1));
    }
  }
  static void reg_global_remove(void *, wl_registry *, uint32_t) {}
  static const wl_registry_listener s_registry_listener = { reg_global, reg_global_remove };

  TestClient::TestClient(const std::string &socket, uint32_t argb, int w, int h,
                         Deco deco) {
    impl = new Impl();
    impl->argb = argb;
    impl->w = w;
    impl->h = h;
    impl->deco = deco;
    impl->display = wl_display_connect(socket.c_str());
    if (!impl->display) return;
    impl->registry = wl_display_get_registry(impl->display);
    wl_registry_add_listener(impl->registry, &s_registry_listener, impl);
    wl_display_flush(impl->display);
  }

  TestClient::~TestClient() {
    if (impl->display) {
      if (impl->pointer) wl_pointer_destroy(impl->pointer);
      if (impl->seat) wl_seat_destroy(impl->seat);
      if (impl->decoration) zxdg_toplevel_decoration_v1_destroy(impl->decoration);
      if (impl->toplevel) xdg_toplevel_destroy(impl->toplevel);
      if (impl->xdgsurf) xdg_surface_destroy(impl->xdgsurf);
      if (impl->surface) wl_surface_destroy(impl->surface);
      if (impl->buffer) wl_buffer_destroy(impl->buffer);
      wl_display_flush(impl->display);
      wl_display_disconnect(impl->display);
    }
    delete impl;
  }

  bool TestClient::ok() const { return impl && impl->display; }

  bool TestClient::gotCloseRequest() const { return impl && impl->got_close; }

  int TestClient::pointerButtonEvents() const { return impl ? impl->pointer_buttons : 0; }

  void TestClient::flush() {
    if (impl->display) wl_display_flush(impl->display);
  }

  void TestClient::closeWindow() {
    if (!impl->display) return;
    if (impl->decoration) { zxdg_toplevel_decoration_v1_destroy(impl->decoration); impl->decoration = nullptr; }
    if (impl->toplevel) { xdg_toplevel_destroy(impl->toplevel); impl->toplevel = nullptr; }
    if (impl->xdgsurf)  { xdg_surface_destroy(impl->xdgsurf);   impl->xdgsurf = nullptr; }
    if (impl->surface)  { wl_surface_destroy(impl->surface);    impl->surface = nullptr; }
    if (impl->buffer)   { wl_buffer_destroy(impl->buffer);      impl->buffer = nullptr; }
    wl_display_flush(impl->display);
  }

  void TestClient::destroyDecorationForTest() {
    if (impl->decoration) {
      zxdg_toplevel_decoration_v1_destroy(impl->decoration);
      impl->decoration = nullptr;
      wl_display_flush(impl->display);
    }
  }

  void TestClient::pump() {
    if (!impl->display) return;

    // Non-blocking read+dispatch of any events the server has sent.
    while (wl_display_prepare_read(impl->display) != 0)
      wl_display_dispatch_pending(impl->display);
    wl_display_flush(impl->display);
    pollfd pfd = { wl_display_get_fd(impl->display), POLLIN, 0 };
    if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN))
      wl_display_read_events(impl->display);
    else
      wl_display_cancel_read(impl->display);
    wl_display_dispatch_pending(impl->display);

    // Once the needed globals are bound, create the toplevel exactly once.
    if (impl->compositor && impl->wm_base && impl->shm && !impl->created) {
      createToplevel(impl);
      impl->created = true;
    }
    wl_display_flush(impl->display);
  }

} // namespace bbai::test
