#include "Server.hh"

#include <cstring>
#include <cstdio>
#include <string>

int main(int argc, char **argv) {
  bool headless = false;
  std::string rc;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--headless") == 0) headless = true;
    else if (std::strcmp(argv[i], "-rc") == 0 && i + 1 < argc) rc = argv[++i];
  }

  bbai::Server server(headless, rc);
  if (!server.ok()) {
    std::fprintf(stderr, "blackboxai: failed to initialise the compositor\n");
    return 1;
  }
  server.run();
  return 0;
}
