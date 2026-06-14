#include "Server.hh"

#include <cstring>
#include <cstdio>

int main(int argc, char **argv) {
  bool headless = false;
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--headless") == 0) headless = true;

  bbai::Server server(headless);
  if (!server.ok()) {
    std::fprintf(stderr, "blackboxai: failed to initialise the compositor\n");
    return 1;
  }
  server.run();
  return 0;
}
