#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "Server.hh"
#include "ClipboardImage.hh"
#include "Screenshot.hh"

#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <cstring>
#include <memory>

using namespace bbai;

static ClipboardImage::Blob makeBlob() {
  std::vector<uint32_t> px(4 * 3, 0xFF112233u);
  return std::make_shared<const std::vector<uint8_t>>(screenshot::encodePng(px, 4, 3));
}

TEST_CASE("ClipboardImage offers image/png and serves the bytes over a pipe") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());

  auto blob = makeBlob();
  REQUIRE(blob->size() > 8);
  ClipboardImage *ci = ClipboardImage::create(server.display, blob);
  REQUIRE(ci != nullptr);

  // Exactly one offered mime type: image/png.
  REQUIRE(ci->base.mime_types.size / sizeof(char *) == 1);
  char **mt = static_cast<char **>(ci->base.mime_types.data);
  CHECK(std::strcmp(mt[0], "image/png") == 0);

  int fds[2];
  REQUIRE(pipe(fds) == 0);
  int rfd = fds[0], wfd = fds[1];
  fcntl(rfd, F_SETFL, fcntl(rfd, F_GETFL, 0) | O_NONBLOCK);

  // Drive the real vtable: send() takes ownership of wfd and registers the writer.
  ci->base.impl->send(&ci->base, "image/png", wfd);

  std::vector<uint8_t> got;
  for (int i = 0; i < 2000; ++i) {
    server.dispatch();
    uint8_t buf[4096];
    ssize_t n = read(rfd, buf, sizeof buf);
    if (n > 0) got.insert(got.end(), buf, buf + n);
    else if (n == 0) break;            // writer closed wfd -> EOF
    // n<0 EAGAIN: keep pumping
  }
  close(rfd);
  CHECK(got == *blob);
  CHECK(fcntl(wfd, F_GETFD) == -1);     // writer closed the write end

  // Destroy: wlroots would call impl->destroy on selection replacement; do it directly.
  ci->base.impl->destroy(&ci->base);    // no crash, frees the C++ subtype
}

TEST_CASE("ClipboardImage writer cleans up when the reader hangs up early") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);
  Server server(/*headless=*/true);
  REQUIRE(server.ok());

  ClipboardImage *ci = ClipboardImage::create(server.display, makeBlob());
  int fds[2];
  REQUIRE(pipe(fds) == 0);
  close(fds[0]);                        // reader gone before any byte is read
  ci->base.impl->send(&ci->base, "image/png", fds[1]);
  for (int i = 0; i < 50; ++i) server.dispatch();   // writer hits EPIPE/HANGUP
  CHECK(fcntl(fds[1], F_GETFD) == -1);  // writer closed its fd, removed its source
  ci->base.impl->destroy(&ci->base);
}
