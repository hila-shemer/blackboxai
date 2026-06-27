// M7: the shipped wayland-sessions entry must stay well-formed, or a display
// manager won't list (or will mis-launch) the session. BBAI_SESSION_DESKTOP is
// the source-tree path, wired by meson.
#include <doctest/doctest.h>
#include <fstream>
#include <sstream>
#include <string>

TEST_CASE("blackboxai.desktop is a well-formed wayland session") {
  std::ifstream f(BBAI_SESSION_DESKTOP); REQUIRE(f.good());
  std::stringstream ss; ss << f.rdbuf(); std::string s = ss.str();
  CHECK(s.find("[Desktop Entry]") != std::string::npos);
  CHECK(s.find("\nExec=blackboxai") != std::string::npos);
  CHECK(s.find("\nType=Application") != std::string::npos);
  CHECK(s.find("\nName=BlackboxAI") != std::string::npos);
}
