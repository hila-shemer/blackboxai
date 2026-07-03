#include "LockTestClient.hh"

#include <wayland-client.h>
#include "ext-session-lock-v1-client-protocol.h"
#include "ext-idle-notify-v1-client-protocol.h"

#include <cstring>
#include <poll.h>
#include <unistd.h>
#include <sys/mman.h>
#include <vector>

namespace bbai::test {

  namespace {

    // Non-blocking read+dispatch (mirrors TestClient::pump's dance).
    void pumpDisplay(wl_display *d) {
      while (wl_display_prepare_read(d) != 0)
        wl_display_dispatch_pending(d);
      wl_display_flush(d);
      pollfd pfd = { wl_display_get_fd(d), POLLIN, 0 };
      if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN))
        wl_display_read_events(d);
      else
        wl_display_cancel_read(d);
      wl_display_dispatch_pending(d);
    }

    [[maybe_unused]] wl_buffer *makeShmBuffer(wl_shm *shm, int w, int h,
                                              uint32_t argb) {
      const int stride = w * 4;
      const int size = stride * h;
      int fd = memfd_create("bbai-lock-shm", MFD_CLOEXEC);
      if (fd < 0) return nullptr;
      if (ftruncate(fd, size) < 0) { close(fd); return nullptr; }
      void *map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
      if (map == MAP_FAILED) { close(fd); return nullptr; }
      uint32_t *px = static_cast<uint32_t *>(map);
      for (int i = 0; i < w * h; ++i) px[i] = argb;
      wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
      wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride,
                                                 WL_SHM_FORMAT_ARGB8888);
      wl_shm_pool_destroy(pool);
      munmap(map, size);
      close(fd);
      return buf;
    }

  } // namespace

  // ---- LockTestClient ---------------------------------------------------------

  // One client-side lock surface (created against one wl_output).
  struct LockSurfaceState {
    LockTestClient::Impl *client = nullptr;
    wl_surface *surface = nullptr;
    ext_session_lock_surface_v1 *lock_surface = nullptr;
    wl_buffer *buffer = nullptr;
    uint32_t argb = 0;
    int configured_w = -1, configured_h = -1;
  };

  struct LockTestClient::Impl {
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    wl_compositor *compositor = nullptr;
    wl_shm *shm = nullptr;
    ext_session_lock_manager_v1 *manager = nullptr;
    ext_session_lock_v1 *lock = nullptr;
    bool saw_idle_notifier = false;
    std::vector<wl_output *> outputs;
    std::vector<LockSurfaceState *> surfaces;
    bool locked = false;
    bool finished = false;
  };

  static void lk_reg_global(void *data, wl_registry *reg, uint32_t name,
                            const char *iface, uint32_t) {
    auto *c = static_cast<LockTestClient::Impl *>(data);
    if (std::strcmp(iface, wl_compositor_interface.name) == 0) {
      c->compositor = static_cast<wl_compositor *>(
        wl_registry_bind(reg, name, &wl_compositor_interface, 4));
    } else if (std::strcmp(iface, wl_shm_interface.name) == 0) {
      c->shm = static_cast<wl_shm *>(
        wl_registry_bind(reg, name, &wl_shm_interface, 1));
    } else if (std::strcmp(iface, wl_output_interface.name) == 0) {
      c->outputs.push_back(static_cast<wl_output *>(
        wl_registry_bind(reg, name, &wl_output_interface, 1)));
    } else if (std::strcmp(iface, ext_session_lock_manager_v1_interface.name) == 0) {
      c->manager = static_cast<ext_session_lock_manager_v1 *>(
        wl_registry_bind(reg, name, &ext_session_lock_manager_v1_interface, 1));
    } else if (std::strcmp(iface, ext_idle_notifier_v1_interface.name) == 0) {
      c->saw_idle_notifier = true;   // observed only; IdleTestClient binds it
    }
  }
  static void lk_reg_remove(void *, wl_registry *, uint32_t) {}
  static const wl_registry_listener s_lk_registry_listener = {
    lk_reg_global, lk_reg_remove };

  LockTestClient::LockTestClient(const std::string &socket) {
    impl = new Impl();
    impl->display = wl_display_connect(socket.c_str());
    if (!impl->display) return;
    impl->registry = wl_display_get_registry(impl->display);
    wl_registry_add_listener(impl->registry, &s_lk_registry_listener, impl);
    wl_display_flush(impl->display);
  }

  LockTestClient::~LockTestClient() {
    if (impl->display) {
      // Deliberately NO ext_session_lock_v1_destroy while locked: destroying a
      // locked lock is a protocol error (INVALID_DESTROY). Dropping the whole
      // connection models a crashed locker - the abandon path Task 8 tests.
      for (LockSurfaceState *s : impl->surfaces) {
        if (s->lock_surface) ext_session_lock_surface_v1_destroy(s->lock_surface);
        if (s->surface) wl_surface_destroy(s->surface);
        if (s->buffer) wl_buffer_destroy(s->buffer);
        delete s;
      }
      if (impl->lock && !impl->locked)
        ext_session_lock_v1_destroy(impl->lock);   // legal before `locked`
      wl_display_flush(impl->display);
      wl_display_disconnect(impl->display);
    }
    delete impl;
  }

  bool LockTestClient::ok() const { return impl && impl->display; }
  void LockTestClient::flush() { if (impl->display) wl_display_flush(impl->display); }
  void LockTestClient::pump() { if (impl->display) pumpDisplay(impl->display); }

  int LockTestClient::outputCount() const {
    return static_cast<int>(impl->outputs.size());
  }
  bool LockTestClient::sawLockManager() const { return impl->manager != nullptr; }
  bool LockTestClient::sawIdleNotifier() const { return impl->saw_idle_notifier; }

  // Grown in Task 3.
  void LockTestClient::lock() {}
  bool LockTestClient::lockedReceived() const { return impl->locked; }
  bool LockTestClient::finishedReceived() const { return impl->finished; }
  void LockTestClient::unlockAndDestroy() {}

  // Grown in Task 4.
  void LockTestClient::createLockSurface(int, uint32_t) {}
  int LockTestClient::configuredWidth(int i) const {
    return i < static_cast<int>(impl->surfaces.size())
      ? impl->surfaces[i]->configured_w : -1;
  }
  int LockTestClient::configuredHeight(int i) const {
    return i < static_cast<int>(impl->surfaces.size())
      ? impl->surfaces[i]->configured_h : -1;
  }

  // ---- IdleTestClient (grown in Task 10) --------------------------------------

  struct IdleTestClient::Impl {
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    wl_seat *seat = nullptr;
    ext_idle_notifier_v1 *notifier = nullptr;
    ext_idle_notification_v1 *notification = nullptr;
    int idled = 0, resumed = 0;
  };

  static void idle_reg_global(void *data, wl_registry *reg, uint32_t name,
                              const char *iface, uint32_t) {
    auto *c = static_cast<IdleTestClient::Impl *>(data);
    if (std::strcmp(iface, wl_seat_interface.name) == 0) {
      c->seat = static_cast<wl_seat *>(
        wl_registry_bind(reg, name, &wl_seat_interface, 1));
    } else if (std::strcmp(iface, ext_idle_notifier_v1_interface.name) == 0) {
      c->notifier = static_cast<ext_idle_notifier_v1 *>(
        wl_registry_bind(reg, name, &ext_idle_notifier_v1_interface, 1));
    }
  }
  static void idle_reg_remove(void *, wl_registry *, uint32_t) {}
  static const wl_registry_listener s_idle_registry_listener = {
    idle_reg_global, idle_reg_remove };

  IdleTestClient::IdleTestClient(const std::string &socket) {
    impl = new Impl();
    impl->display = wl_display_connect(socket.c_str());
    if (!impl->display) return;
    impl->registry = wl_display_get_registry(impl->display);
    wl_registry_add_listener(impl->registry, &s_idle_registry_listener, impl);
    wl_display_flush(impl->display);
  }

  IdleTestClient::~IdleTestClient() {
    if (impl->display) {
      if (impl->notification) ext_idle_notification_v1_destroy(impl->notification);
      if (impl->notifier) ext_idle_notifier_v1_destroy(impl->notifier);
      if (impl->seat) wl_seat_destroy(impl->seat);
      wl_display_flush(impl->display);
      wl_display_disconnect(impl->display);
    }
    delete impl;
  }

  bool IdleTestClient::ok() const { return impl && impl->display; }
  void IdleTestClient::flush() { if (impl->display) wl_display_flush(impl->display); }
  void IdleTestClient::pump() { if (impl->display) pumpDisplay(impl->display); }

  void IdleTestClient::createNotification(uint32_t) {}
  int IdleTestClient::idledCount() const { return impl->idled; }
  int IdleTestClient::resumedCount() const { return impl->resumed; }

} // namespace bbai::test
