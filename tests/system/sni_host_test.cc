// sni-core system tests. Every case runs on a PRIVATE session bus - meson
// wraps this exe in dbus-run-session - so claiming org.kde.StatusNotifierWatcher
// never touches the developer's real tray.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "SniHost.hh"
#include "SniMockItem.hh"

#include <systemd/sd-bus.h>

#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <unistd.h>
#include <vector>

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

  struct SigWatch {
    bool fired = false;
    std::string arg;
  };

  int onSig(sd_bus_message *m, void *userdata, sd_bus_error *) {
    auto *w = static_cast<SigWatch *>(userdata);
    const char *s = nullptr;
    if (sd_bus_message_read(m, "s", &s) >= 0 && s) w->arg = s;
    w->fired = true;
    return 0;
  }

  // Pump the host + any raw connections until pred() or ~3s. Non-blocking
  // throughout - the host and the observers all live in this process.
  template <typename P>
  bool pumpUntil(Host &host, std::initializer_list<sd_bus *> conns, P pred) {
    for (int i = 0; i < 600; ++i) {
      host.processForTest();
      for (sd_bus *c : conns)
        while (sd_bus_process(c, nullptr) > 0) {}
      if (pred()) return true;
      usleep(5000);
    }
    return false;
  }

  // Reading a WATCHER property is special: the watcher lives in THIS process,
  // so a blocking sd_bus_get_property would deadlock (nobody pumps the host
  // while the caller waits - the reply can never be produced). Async + pump.
  struct PropReply {
    bool done = false;
    std::vector<std::string> strings;   // filled for an 'as' property
    int boolean = -1;                   // filled for a 'b' property
  };

  int onPropReply(sd_bus_message *reply, void *userdata, sd_bus_error *) {
    auto *pr = static_cast<PropReply *>(userdata);
    pr->done = true;
    if (sd_bus_message_is_method_error(reply, nullptr)) return 0;
    char type = 0;
    const char *contents = nullptr;
    if (sd_bus_message_peek_type(reply, &type, &contents) < 0 || !contents) return 0;
    if (sd_bus_message_enter_container(reply, 'v', contents) < 0) return 0;
    if (strcmp(contents, "as") == 0) {
      sd_bus_message_enter_container(reply, 'a', "s");
      const char *s = nullptr;
      while (sd_bus_message_read(reply, "s", &s) > 0)
        pr->strings.push_back(s);
      sd_bus_message_exit_container(reply);
    } else if (strcmp(contents, "b") == 0) {
      int b = 0;
      if (sd_bus_message_read(reply, "b", &b) >= 0) pr->boolean = b;
    }
    sd_bus_message_exit_container(reply);
    return 0;
  }

  PropReply watcherProp(Host &host, sd_bus *conn, const char *prop) {
    PropReply pr;
    sd_bus_slot *slot = nullptr;
    sd_bus_call_method_async(conn, &slot, "org.kde.StatusNotifierWatcher",
                             "/StatusNotifierWatcher",
                             "org.freedesktop.DBus.Properties", "Get",
                             onPropReply, &pr, "ss",
                             "org.kde.StatusNotifierWatcher", prop);
    pumpUntil(host, {conn}, [&] { return pr.done; });
    if (slot) sd_bus_slot_unref(slot);   // cancels the callback on timeout - &pr dies here
    return pr;
  }

  std::vector<std::string> registeredItems(Host &host, sd_bus *conn) {
    return watcherProp(host, conn, "RegisteredStatusNotifierItems").strings;
  }

  // A bare item object so the Host's post-registration GetAll succeeds with an
  // empty dict (sd-bus serves org.freedesktop.DBus.Properties for any vtable).
  // Without it sd-bus auto-replies UnknownObject and the Host - correctly, per
  // its materialize-or-drop rule - unregisters the item before we can assert.
  const sd_bus_vtable kEmptyItemVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_VTABLE_END
  };

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

TEST_CASE("path-variant registration: signal fires, property lists service+path") {
  Host host(nullptr);
  REQUIRE(host.ok());

  sd_bus *observer = nullptr;
  REQUIRE(sd_bus_open_user(&observer) >= 0);
  SigWatch reg_sig;
  REQUIRE(sd_bus_match_signal(observer, nullptr, nullptr, "/StatusNotifierWatcher",
                              "org.kde.StatusNotifierWatcher",
                              "StatusNotifierItemRegistered", onSig, &reg_sig) >= 0);

  sd_bus *item_conn = nullptr;                 // plays the app
  REQUIRE(sd_bus_open_user(&item_conn) >= 0);
  REQUIRE(sd_bus_add_object_vtable(item_conn, nullptr, "/StatusNotifierItem",
                                   "org.kde.StatusNotifierItem",
                                   kEmptyItemVtable, nullptr) >= 0);
  // Async, not blocking: the watcher lives in THIS process and only serves the
  // call when we pump it - a blocking call here would deadlock.
  REQUIRE(sd_bus_call_method_async(item_conn, nullptr,
                                   "org.kde.StatusNotifierWatcher",
                                   "/StatusNotifierWatcher",
                                   "org.kde.StatusNotifierWatcher",
                                   "RegisterStatusNotifierItem", nullptr, nullptr,
                                   "s", "/StatusNotifierItem") >= 0);

  CHECK(pumpUntil(host, {observer, item_conn}, [&] { return reg_sig.fired; }));

  std::vector<std::string> items = registeredItems(host, observer);
  REQUIRE(items.size() == 1);
  CHECK(items[0][0] == ':');                   // sender's unique name...
  CHECK(items[0].find("/StatusNotifierItem") != std::string::npos);  // ...+ path
  CHECK(reg_sig.arg == items[0]);

  sd_bus_flush_close_unref(item_conn);
  sd_bus_flush_close_unref(observer);
}

TEST_CASE("name-variant registration resolves to the well-known name") {
  Host host(nullptr);
  REQUIRE(host.ok());

  sd_bus *observer = nullptr;
  REQUIRE(sd_bus_open_user(&observer) >= 0);
  SigWatch reg_sig;
  REQUIRE(sd_bus_match_signal(observer, nullptr, nullptr, "/StatusNotifierWatcher",
                              "org.kde.StatusNotifierWatcher",
                              "StatusNotifierItemRegistered", onSig, &reg_sig) >= 0);

  sd_bus *item_conn = nullptr;
  REQUIRE(sd_bus_open_user(&item_conn) >= 0);
  REQUIRE(sd_bus_add_object_vtable(item_conn, nullptr, "/StatusNotifierItem",
                                   "org.kde.StatusNotifierItem",
                                   kEmptyItemVtable, nullptr) >= 0);
  REQUIRE(sd_bus_request_name(item_conn, "org.test.RawItem", 0) >= 0);
  REQUIRE(sd_bus_call_method_async(item_conn, nullptr,
                                   "org.kde.StatusNotifierWatcher",
                                   "/StatusNotifierWatcher",
                                   "org.kde.StatusNotifierWatcher",
                                   "RegisterStatusNotifierItem", nullptr, nullptr,
                                   "s", "org.test.RawItem") >= 0);

  CHECK(pumpUntil(host, {observer, item_conn}, [&] { return reg_sig.fired; }));
  std::vector<std::string> items = registeredItems(host, observer);
  REQUIRE(items.size() == 1);
  CHECK(items[0] == "org.test.RawItem/StatusNotifierItem");

  sd_bus_flush_close_unref(observer);
  sd_bus_flush_close_unref(item_conn);
}

TEST_CASE("mock publisher registers with the watcher and serves its icon") {
  Host host(nullptr);
  REQUIRE(host.ok());

  bbai::test::SniMockChild mock;
  REQUIRE(mock.ok());
  CHECK(mock.waitReport(5000, [&] { host.processForTest(); }) == "registered");

  // Read the icon back over the bus like POC #1 did - proves the child's
  // vtable serves byte-exact a(iiay). Blocking Get is fine HERE (unlike the
  // watcher-property reads): the target is the child, and it pumps itself.
  sd_bus *probe = nullptr;
  REQUIRE(sd_bus_open_user(&probe) >= 0);
  std::vector<std::string> items = registeredItems(host, probe);
  REQUIRE(items.size() == 1);
  std::string service = items[0].substr(0, items[0].find('/'));

  sd_bus_error err = SD_BUS_ERROR_NULL;
  sd_bus_message *reply = nullptr;
  REQUIRE(sd_bus_get_property(probe, service.c_str(), "/StatusNotifierItem",
                              "org.kde.StatusNotifierItem", "IconPixmap",
                              &err, &reply, "a(iiay)") >= 0);
  REQUIRE(sd_bus_message_enter_container(reply, 'a', "(iiay)") >= 0);
  REQUIRE(sd_bus_message_enter_container(reply, 'r', "iiay") >= 0);
  int32_t w = 0, h = 0;
  const void *bytes = nullptr;
  size_t len = 0;
  REQUIRE(sd_bus_message_read(reply, "ii", &w, &h) >= 0);
  REQUIRE(sd_bus_message_read_array(reply, 'y', &bytes, &len) >= 0);
  CHECK(w == 2);
  CHECK(h == 2);
  REQUIRE(len == 16);
  CHECK(memcmp(bytes, bbai::test::kSniMockIcon, 16) == 0);
  sd_bus_message_unref(reply);
  sd_bus_error_free(&err);

  mock.quit();
  sd_bus_flush_close_unref(probe);
}

TEST_CASE("registration materializes a full sni::Item") {
  Host host(nullptr);
  REQUIRE(host.ok());
  std::vector<Item> added;
  HostEvents ev;
  ev.itemAdded = [&](const Item &it) { added.push_back(it); };
  host.setEvents(std::move(ev));

  bbai::test::SniMockChild mock;
  REQUIRE(mock.ok());
  REQUIRE(mock.waitReport(5000, [&] { host.processForTest(); }) == "registered");

  REQUIRE(pumpUntil(host, {}, [&] { return !host.items().empty(); }));
  REQUIRE(added.size() == 1);
  const Item &it = host.items()[0];
  CHECK(it.service[0] == ':');            // path-variant: unique name
  CHECK(it.path == "/StatusNotifierItem");
  CHECK(it.id == "sni-mock");
  CHECK(it.title == "Mock Item");
  CHECK(it.status == "Active");
  CHECK(it.icon_name == "mock-icon");
  CHECK(it.icon_theme_path == "/tmp/mock-theme");
  CHECK(it.menu_path == "/MenuBar");
  CHECK(it.item_is_menu);
  CHECK(it.tooltip == "Mock Tooltip");
  REQUIRE(it.icon_pixmaps.size() == 1);
  CHECK(it.icon_pixmaps[0].width == 2);
  CHECK(it.icon_pixmaps[0].height == 2);
  REQUIRE(it.icon_pixmaps[0].data.size() == 16);
  CHECK(memcmp(it.icon_pixmaps[0].data.data(), bbai::test::kSniMockIcon, 16) == 0);
  mock.quit();
}

TEST_CASE("name-variant registration materializes under the well-known name") {
  Host host(nullptr);
  REQUIRE(host.ok());
  bbai::test::SniMockChild mock(/*register_by_name=*/true);
  REQUIRE(mock.ok());
  REQUIRE(mock.waitReport(5000, [&] { host.processForTest(); }) == "registered");
  REQUIRE(pumpUntil(host, {}, [&] { return !host.items().empty(); }));
  CHECK(host.items()[0].service == "org.test.SniMock");
  mock.quit();
}

TEST_CASE("NewIcon / NewStatus re-fetch and fire itemChanged") {
  Host host(nullptr);
  REQUIRE(host.ok());
  int changed = 0;
  HostEvents ev;
  ev.itemChanged = [&](const Item &) { ++changed; };
  host.setEvents(std::move(ev));

  bbai::test::SniMockChild mock;
  REQUIRE(mock.ok());
  REQUIRE(mock.waitReport(5000, [&] { host.processForTest(); }) == "registered");
  REQUIRE(pumpUntil(host, {}, [&] { return !host.items().empty(); }));

  mock.updateIcon();
  REQUIRE(pumpUntil(host, {}, [&] { return changed >= 1; }));
  REQUIRE(host.items()[0].icon_pixmaps.size() == 1);
  CHECK(memcmp(host.items()[0].icon_pixmaps[0].data.data(),
               bbai::test::kSniMockIcon2, 16) == 0);

  mock.updateStatus();
  REQUIRE(pumpUntil(host, {}, [&] { return changed >= 2; }));
  CHECK(host.items()[0].status == "NeedsAttention");
  mock.quit();
}
