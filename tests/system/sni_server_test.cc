// sni-core x Server: the Host rides the display's event loop, Server::dispatch
// is the only pump. Runs under dbus-run-session (private bus) + headless env.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "Server.hh"
#include "SniHost.hh"
#include "SniMockItem.hh"

#include <cstdlib>
#include <string>
#include <unistd.h>

using namespace bbai;

TEST_CASE("Server-owned Host materializes items via Server::dispatch") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  REQUIRE(server.sniHostForTest() == nullptr);   // headless ctor skips the bus
  server.createSniHostForTest();
  REQUIRE(server.sniHostForTest()->ok());

  test::SniMockChild mock;
  REQUIRE(mock.ok());
  bool up = false;
  for (int i = 0; i < 600 && !up; ++i) {
    server.dispatch();
    up = !server.sniHostForTest()->items().empty();
    usleep(5000);
  }
  REQUIRE(up);
  CHECK(server.sniHostForTest()->items()[0].id == "sni-mock");
  CHECK(server.sniHost().items().size() == 1);   // the production accessor
  mock.quit();
}

TEST_CASE("no session bus: Host inert, Server unbothered") {
  const char *saved_addr = getenv("DBUS_SESSION_BUS_ADDRESS");
  std::string addr = saved_addr ? saved_addr : "";
  const char *saved_xrd = getenv("XDG_RUNTIME_DIR");
  std::string xrd = saved_xrd ? saved_xrd : "";
  char tmpl[] = "/tmp/bbai-sni-noserver-XXXXXX";
  REQUIRE(mkdtemp(tmpl) != nullptr);
  // XDG_RUNTIME_DIR points at a busless dir so the sd_bus_open_user fallback
  // finds nothing; the wayland socket lands there too, which is fine.
  unsetenv("DBUS_SESSION_BUS_ADDRESS");
  setenv("XDG_RUNTIME_DIR", tmpl, 1);
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  {
    Server server(/*headless=*/true);
    REQUIRE(server.ok());
    server.createSniHostForTest();
    CHECK(!server.sniHostForTest()->ok());
    for (int i = 0; i < 20; ++i)
      server.dispatch();               // must not crash while inert
  }

  if (saved_addr) setenv("DBUS_SESSION_BUS_ADDRESS", addr.c_str(), 1);
  if (saved_xrd) setenv("XDG_RUNTIME_DIR", xrd.c_str(), 1);
  else unsetenv("XDG_RUNTIME_DIR");
}
