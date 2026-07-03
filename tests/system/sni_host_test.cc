// sni-core system tests. Every case runs on a PRIVATE session bus - meson
// wraps this exe in dbus-run-session - so claiming org.kde.StatusNotifierWatcher
// never touches the developer's real tray.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "SniHost.hh"

#include <systemd/sd-bus.h>

#include <cstdlib>
#include <string>
#include <unistd.h>

using namespace bbai::sni;

namespace {

  // Raw probe helper: who owns `name` on the bus? ("" = nobody)
  std::string nameOwner(sd_bus *bus, const char *name) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = nullptr;
    int r = sd_bus_call_method(bus, "org.freedesktop.DBus",
                               "/org/freedesktop/DBus", "org.freedesktop.DBus",
                               "GetNameOwner", &err, &reply, "s", name);
    std::string owner;
    if (r >= 0) {
      const char *s = nullptr;
      if (sd_bus_message_read(reply, "s", &s) >= 0 && s) owner = s;
      sd_bus_message_unref(reply);
    }
    sd_bus_error_free(&err);
    return owner;
  }

} // namespace

TEST_CASE("Host claims the watcher name on the private bus") {
  Host host(nullptr);
  REQUIRE(host.ok());

  sd_bus *probe = nullptr;
  REQUIRE(sd_bus_open_user(&probe) >= 0);
  CHECK(nameOwner(probe, "org.kde.StatusNotifierWatcher") != "");
  sd_bus_flush_close_unref(probe);
}

TEST_CASE("watcher name already taken -> Host goes inert, no crash") {
  sd_bus *squatter = nullptr;
  REQUIRE(sd_bus_open_user(&squatter) >= 0);
  REQUIRE(sd_bus_request_name(squatter, "org.kde.StatusNotifierWatcher", 0) >= 0);

  Host host(nullptr);
  CHECK(!host.ok());
  host.processForTest();                       // inert pump must be harmless

  sd_bus_flush_close_unref(squatter);
}

TEST_CASE("no session bus at all -> inert") {
  // Scrub both roads sd_bus_open_user knows: the explicit address and the
  // XDG_RUNTIME_DIR/bus fallback (pointing the latter at an empty dir keeps a
  // dev machine's real user bus out of reach).
  const char *saved_addr = getenv("DBUS_SESSION_BUS_ADDRESS");
  std::string addr = saved_addr ? saved_addr : "";
  const char *saved_xrd = getenv("XDG_RUNTIME_DIR");
  std::string xrd = saved_xrd ? saved_xrd : "";
  char tmpl[] = "/tmp/bbai-sni-nobus-XXXXXX";
  REQUIRE(mkdtemp(tmpl) != nullptr);
  unsetenv("DBUS_SESSION_BUS_ADDRESS");
  setenv("XDG_RUNTIME_DIR", tmpl, 1);

  {
    Host host(nullptr);
    CHECK(!host.ok());
    host.processForTest();
  }

  if (saved_addr) setenv("DBUS_SESSION_BUS_ADDRESS", addr.c_str(), 1);
  if (saved_xrd) setenv("XDG_RUNTIME_DIR", xrd.c_str(), 1);
  else unsetenv("XDG_RUNTIME_DIR");
  rmdir(tmpl);
}
