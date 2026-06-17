// T6: scene hit-testing maps a cursor position back to a View and the frame
// part under it (titlebar / grips / buttons / client), and passthrough motion
// over the client area gives the client pointer focus. Drives the test-only
// injection API, which funnels into the same onPointer* handlers real cursor
// events use.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>
#include <linux/input-event-codes.h>  // BTN_LEFT

using namespace bbai;

TEST_CASE("cursor position classifies the View and frame part under it") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  test::TestClient client(server.socketName(), 0xFFFF0000u, 200, 150,
                          test::TestClient::Deco::RequestSSD);
  REQUIRE(client.ok());
  auto mapped = [&] {
    const auto &v = server.viewsForTest();
    return !v.empty() && v[0]->isMapped();
  };
  for (int i = 0; i < 500 && !mapped(); ++i) { client.flush(); server.dispatch(); client.pump(); }
  REQUIRE(mapped());
  for (int i = 0; i < 30; ++i) { client.flush(); server.dispatch(); client.pump(); }

  View *v = server.viewsForTest()[0].get();
  REQUIRE(v->drawsFrame());
  // Frame at (160,120): titlebar y[120,143), client (161,143)+200x150,
  // handle/grips y[293,299), left grip x[160,198), right grip x[324,362).

  // Titlebar (drag region) — also resolves the View.
  CHECK(server.viewAtForTest(260, 130) == v);
  CHECK(server.partAtForTest(260, 130) == Part::Titlebar);
  // Buttons in the titlebar.
  CHECK(server.partAtForTest(346, 127) == Part::CloseButton);    // close button
  CHECK(server.partAtForTest(170, 130) == Part::IconifyButton);  // iconify button
  // Resize grips on the handle.
  CHECK(server.partAtForTest(165, 295) == Part::LeftGrip);
  CHECK(server.partAtForTest(350, 295) == Part::RightGrip);
  // Client content.
  CHECK(server.partAtForTest(261, 218) == Part::Client);
  // Empty desktop resolves to no View / no part.
  CHECK(server.viewAtForTest(40, 40) == nullptr);
  CHECK(server.partAtForTest(40, 40) == Part::None);

  // Passthrough motion over the client gives the client pointer focus.
  server.injectPointerMotionForTest(261, 218);
  CHECK(server.focusedPointerSurfaceForTest() == v->toplevel()->base->surface);
  // Motion over the titlebar clears client pointer focus (it's our chrome).
  server.injectPointerMotionForTest(260, 130);
  CHECK(server.focusedPointerSurfaceForTest() == nullptr);
}

TEST_CASE("a button-down drag from the client keeps focus across the chrome boundary") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();
  test::TestClient client(server.socketName(), 0xFFFF0000u, 200, 150,
                          test::TestClient::Deco::RequestSSD);
  REQUIRE(client.ok());
  auto mapped = [&] {
    const auto &v = server.viewsForTest();
    return !v.empty() && v[0]->isMapped();
  };
  for (int i = 0; i < 500 && !mapped(); ++i) { client.flush(); server.dispatch(); client.pump(); }
  REQUIRE(mapped());
  for (int i = 0; i < 30; ++i) { client.flush(); server.dispatch(); client.pump(); }
  View *v = server.viewsForTest()[0].get();
  wlr_surface *surf = v->toplevel()->base->surface;

  // Press inside the client, then drag the cursor up onto the titlebar without
  // releasing: the implicit grab must keep the client focused (so the eventual
  // release reaches it), not re-focus the chrome.
  server.injectPointerMotionForTest(261, 218);             // over client -> focus it
  REQUIRE(server.focusedPointerSurfaceForTest() == surf);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/true);
  server.injectPointerMotionForTest(260, 130);             // drag onto the titlebar
  CHECK(server.focusedPointerSurfaceForTest() == surf);    // focus held, not cleared
  server.injectPointerMotionForTest(40, 40);               // drag off the window
  CHECK(server.focusedPointerSurfaceForTest() == surf);
  server.injectPointerButtonForTest(BTN_LEFT, /*pressed=*/false);  // release reaches client
  // After release the grab ends; moving over chrome now clears focus again.
  server.injectPointerMotionForTest(260, 130);
  CHECK(server.focusedPointerSurfaceForTest() == nullptr);
}
