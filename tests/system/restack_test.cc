// B4: View is a StackEntity; raiseView/lowerView reorder the scene, and
// setOnWorkspace hides/shows a view without losing stacking. Two overlapping
// clients (red under green) at the same position.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"

#include <cstdlib>

using namespace bbai;

TEST_CASE("raise/lower reorder the scene; setOnWorkspace hides a view") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
    server.dispatch();

  test::TestClient a(server.socketName(), 0xFFFF0000u, 200, 150);  // red
  test::TestClient b(server.socketName(), 0xFF00FF00u, 200, 150);  // green
  REQUIRE(a.ok()); REQUIRE(b.ok());
  auto mappedN = [&](size_t n) {
    const auto &v = server.viewsForTest();
    if (v.size() < n) return false;
    for (size_t i = 0; i < n; ++i) if (!v[i]->isMapped()) return false;
    return true;
  };
  for (int i = 0; i < 800 && !mappedN(2); ++i) {
    a.flush(); b.flush(); server.dispatch(); a.pump(); b.pump();
  }
  REQUIRE(mappedN(2));

  View *va = server.viewsForTest()[0].get();  // red (mapped first -> lower)
  View *vb = server.viewsForTest()[1].get();  // green (mapped second -> top)

  auto centre = [&] { return test::captureFrame(server).pixels[218u * 1280 + 261] & 0x00FFFFFFu; };

  // Both windows are at (160,120); the later-mapped green is on top.
  CHECK(centre() == 0x0000FF00u);
  server.raiseView(va);                       // red to the top
  CHECK(centre() == 0x00FF0000u);
  server.lowerView(va);                       // red back to the bottom -> green on top
  CHECK(centre() == 0x0000FF00u);

  // Hide green -> red shows through; stacking is preserved.
  vb->setOnWorkspace(false);
  CHECK_FALSE(vb->visible());
  CHECK(centre() == 0x00FF0000u);
  // Hide red too -> only the gradient desktop remains.
  va->setOnWorkspace(false);
  CHECK(centre() != 0x00FF0000u);
  CHECK(centre() != 0x0000FF00u);

  // Re-show both: green is on top again (stacking order survived hide/show).
  va->setOnWorkspace(true);
  vb->setOnWorkspace(true);
  CHECK(centre() == 0x0000FF00u);
}
