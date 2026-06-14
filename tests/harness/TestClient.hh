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
    // Whether the client negotiates server-side decorations via xdg-decoration.
    //   None       — never binds the protocol (compositor's default policy wins)
    //   RequestSSD — binds + set_mode(server_side)
    //   RequestCSD — binds + set_mode(client_side) (a CSD holdout)
    enum class Deco { None, RequestSSD, RequestCSD };

    // argb is a packed ARGB8888 (premultiplied) constant; the window is w x h.
    TestClient(const std::string &socket, uint32_t argb, int w, int h,
               Deco deco = Deco::None);
    ~TestClient();
    TestClient(const TestClient &) = delete;
    TestClient &operator=(const TestClient &) = delete;

    bool ok() const;
    bool gotCloseRequest() const;  // the compositor sent xdg_toplevel.close
    int pointerButtonEvents() const;  // count of wl_pointer.button events received
    void flush();        // push queued client requests to the compositor
    void pump();         // non-blocking: read+dispatch server events, advance state
    void closeWindow();  // destroy the toplevel/surface (server should drop the View)
    void destroyDecorationForTest();  // destroy ONLY the decoration object (keep the toplevel)

    struct Impl;    // opaque; defined in TestClient.cc

  private:
    Impl *impl;
  };

} // namespace bbai::test

#endif // BLACKBOXAI_TEST_CLIENT_HH
