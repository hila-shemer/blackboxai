// RAII wrapper over the wl_listener-in-struct idiom (after Wayfire's
// wl_listener_wrapper): embeds a wl_listener + a std::function callback, a
// static thunk recovers `this` via wl_container_of, and the destructor
// disconnects. Non-copyable. This maps wlroots' C signal plumbing onto the
// callback style the bt:: toolkit already uses.
#ifndef BLACKBOXAI_LISTENER_HPP
#define BLACKBOXAI_LISTENER_HPP

#include "wlr.hpp"
#include <functional>

namespace bt {

  class Listener {
  public:
    using Cb = std::function<void(void *data)>;

    Listener() { wl.notify = &Listener::thunk; wl_list_init(&wl.link); }
    ~Listener() { disconnect(); }
    Listener(const Listener &) = delete;
    Listener &operator=(const Listener &) = delete;

    void connect(wl_signal *signal, Cb cb) {
      disconnect();
      callback = std::move(cb);
      wl_signal_add(signal, &wl);
    }
    void disconnect() {
      if (!wl_list_empty(&wl.link)) {
        wl_list_remove(&wl.link);
        wl_list_init(&wl.link);
      }
      callback = nullptr;
    }

  private:
    static void thunk(wl_listener *l, void *data) {
      Listener *self = wl_container_of(l, self, wl);
      if (self->callback) self->callback(data);
    }
    wl_listener wl;
    Cb callback;
  };

} // namespace bt

#endif // BLACKBOXAI_LISTENER_HPP
