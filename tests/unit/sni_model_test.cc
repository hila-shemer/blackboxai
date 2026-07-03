// sni-core L0: the pure model helpers - registration-arg resolution, network-
// order icon conversion, best-size pick. No bus anywhere near this TU.
#include <doctest/doctest.h>
#include "Sni.hh"

using namespace bbai::sni;

TEST_CASE("resolveRegistration: path arg takes the service from the sender") {
  Registration r = resolveRegistration("/StatusNotifierItem", ":1.42");
  CHECK(r.service == ":1.42");
  CHECK(r.path == "/StatusNotifierItem");
}

TEST_CASE("resolveRegistration: name arg gets the spec default path") {
  Registration r = resolveRegistration("org.kde.someapp", ":1.42");
  CHECK(r.service == "org.kde.someapp");
  CHECK(r.path == "/StatusNotifierItem");
}

TEST_CASE("toNative packs network-order ARGB bytes into 0xAARRGGBB") {
  IconFrame f;
  f.width = 2;
  f.height = 1;
  f.data = { 0xff, 0x10, 0x20, 0x30,    // A R G B
             0x80, 0x01, 0x02, 0x03 };
  std::vector<uint32_t> px = f.toNative();
  REQUIRE(px.size() == 2);
  CHECK(px[0] == 0xff102030u);
  CHECK(px[1] == 0x80010203u);
}

TEST_CASE("pickBestFrame prefers the smallest frame >= target") {
  std::vector<IconFrame> frames(3);
  frames[0].width = frames[0].height = 16;
  frames[1].width = frames[1].height = 22;
  frames[2].width = frames[2].height = 48;
  CHECK(pickBestFrame(frames, 22) == &frames[1]);
  CHECK(pickBestFrame(frames, 24) == &frames[2]);  // 22 < target; 48 wins
  CHECK(pickBestFrame(frames, 64) == &frames[2]);  // nothing >= 64: closest wins
  CHECK(pickBestFrame({}, 22) == nullptr);
}
