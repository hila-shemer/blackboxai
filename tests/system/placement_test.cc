// Window placement at map: each session.windowPlacement policy lands a freshly
// mapped frame where the policy dictates, off the live workArea + frame metrics
// (never a baked coordinate). Two clients per case; the SECOND proves the policy
// (the first just occupies a slot).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "TestClient.hh"
#include "Server.hh"
#include "View.hh"
#include "Output.hh"
#include "Frame.hh"

#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

using namespace bbai;

namespace {
  void boot(Server &s) {
    REQUIRE(s.ok());
    for (int i = 0; i < 50 && s.activeSceneOutputForTest() == nullptr; ++i) s.dispatch();
  }
  std::string writeRc(const std::string &body) {
    char t[] = "/tmp/bbai-place-XXXXXX";
    REQUIRE(mkdtemp(t) != nullptr);
    std::string p = std::string(t) + "/rc";
    std::ofstream(p) << body;
    return p;
  }
  // Map a client and return its View WITHOUT repositioning (we observe placement).
  View *mapNext(Server &s, test::TestClient &c) {
    REQUIRE(c.ok());
    const size_t n0 = s.viewsForTest().size();
    auto mapped = [&] { const auto &v = s.viewsForTest();
                        return v.size() > n0 && v.back()->isMapped(); };
    for (int i = 0; i < 500 && !mapped(); ++i) { c.flush(); s.dispatch(); c.pump(); }
    REQUIRE(mapped());
    for (int i = 0; i < 30; ++i) { c.flush(); s.dispatch(); c.pump(); }
    return s.viewsForTest().back().get();
  }
}

TEST_CASE("Center places the mapped frame centred in the work area") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  const std::string rc = writeRc("session.windowPlacement: CenterPlacement\n");
  Server server(/*headless=*/true, rc);
  boot(server);
  test::TestClient c(server.socketName(), 0xFFFF0000u, 200, 150,
                     test::TestClient::Deco::RequestSSD);
  View *v = mapNext(server, c);
  const wlr_box work = server.activeOutputForTest()->workArea();
  const int fw = frame::frameWidth(v->contentWidth());
  const int fh = frame::frameHeight(v->contentHeight());
  CHECK(v->x() == work.x + (work.width  - fw) / 2);
  CHECK(v->y() == work.y + (work.height - fh) / 2);
}

TEST_CASE("Cascade steps the second window down-right of the first") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  const std::string rc = writeRc("session.windowPlacement: CascadePlacement\n");
  Server server(/*headless=*/true, rc);
  boot(server);
  test::TestClient c1(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *a = mapNext(server, c1);
  test::TestClient c2(server.socketName(), 0xFF0000FFu, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *b = mapNext(server, c2);
  CHECK(b->x() > a->x());
  CHECK(b->y() > a->y());
}

TEST_CASE("RowSmart avoids overlapping the first window") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  const std::string rc = writeRc("session.windowPlacement: RowSmartPlacement\n");
  Server server(/*headless=*/true, rc);
  boot(server);
  test::TestClient c1(server.socketName(), 0xFFFF0000u, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *a = mapNext(server, c1);
  test::TestClient c2(server.socketName(), 0xFF0000FFu, 200, 150,
                      test::TestClient::Deco::RequestSSD);
  View *b = mapNext(server, c2);
  // Distinct slots: the second frame is not stacked on top of the first.
  const bool same = (a->x() == b->x() && a->y() == b->y());
  CHECK_FALSE(same);
}
