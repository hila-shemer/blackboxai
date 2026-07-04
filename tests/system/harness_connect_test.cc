// The harness clients' bounded-retry connect (ConnectRetry.hh): survives a
// socket that appears late (the recorded cold-cache load flake shape), and
// still gives up within its bound when no server ever comes up. Pure
// client-side TU - no compositor involved.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "ConnectRetry.hh"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

TEST_CASE("connectWithRetry survives a socket that appears late") {
  const char *rt = getenv("XDG_RUNTIME_DIR");
  REQUIRE(rt != nullptr);
  const std::string name = "bbai-retry-" + std::to_string(getpid());
  const std::string path = std::string(rt) + "/" + name;
  unlink(path.c_str());

  // No doctest asserts inside the thread (doctest is single-threaded);
  // record and check after join.
  int lfd = -1;
  bool listening = false;
  std::thread late([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lfd < 0) return;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    listening = bind(lfd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0
             && listen(lfd, 8) == 0;
  });

  wl_display *d = bbai::test::connectWithRetry(name.c_str());
  late.join();
  REQUIRE(listening);
  CHECK(d != nullptr);       // a one-shot connect would have returned null
  if (d) wl_display_disconnect(d);
  if (lfd >= 0) close(lfd);
  unlink(path.c_str());
}

TEST_CASE("connectWithRetry gives up within its bound when no server appears") {
  const auto t0 = std::chrono::steady_clock::now();
  wl_display *d = bbai::test::connectWithRetry("bbai-never-exists");
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();
  CHECK(d == nullptr);
  CHECK(ms < 5000);          // 50 x 10ms + connect overhead, generous margin
}
