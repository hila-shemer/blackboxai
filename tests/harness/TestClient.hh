// An in-process libwayland client that maps one xdg_toplevel filled with a
// solid known color (via wl_shm). Deliberately OPAQUE — it does not include
// wayland-client.h — so it can live in the same test binary as the wayland
// *server* side (struct wl_display clashes between the two libraries).
//
// Drive it from the test loop, interleaving with the compositor:
//     client.flush(); server.dispatch(); client.pump();   // repeat
// until the server reports the surface mapped, then capture the frame.
#ifndef BLACKBOXAI_TEST_CLIENT_HH
#define BLACKBOXAI_TEST_CLIENT_HH

#include <string>
#include <cstdint>

namespace bbai::test {

  class TestClient {
  public:
    // argb is a packed ARGB8888 (premultiplied) constant; the window is w x h.
    TestClient(const std::string &socket, uint32_t argb, int w, int h);
    ~TestClient();
    TestClient(const TestClient &) = delete;
    TestClient &operator=(const TestClient &) = delete;

    bool ok() const;
    void flush();        // push queued client requests to the compositor
    void pump();         // non-blocking: read+dispatch server events, advance state
    void closeWindow();  // destroy the toplevel/surface (server should drop the View)

    struct Impl;    // opaque; defined in TestClient.cc

  private:
    Impl *impl;
  };

} // namespace bbai::test

#endif // BLACKBOXAI_TEST_CLIENT_HH
