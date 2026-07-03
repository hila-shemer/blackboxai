#include "Server.hh"

#include <cstring>
#include <cstdio>
#include <string>
#include <unistd.h>   // execvp
#include <vector>

int main(int argc, char **argv) {
  bool headless = false;
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--headless") == 0) headless = true;

  bool restart = false;
  std::vector<std::string> restart_argv;
  {
    bbai::Server server(headless);
    if (!server.ok()) {
      std::fprintf(stderr, "blackboxai: failed to initialise the compositor\n");
      return 1;
    }
    server.run();
    restart = server.restartRequested();
    restart_argv = server.restartArgv();
  }   // full teardown first - the exec below must not inherit a live display

  if (restart) {
    // Restart on Wayland, documented plainly: the compositor IS the display
    // server, so a restart (self or another WM) takes every client down with
    // it. Classic shape otherwise (blackbox.cc restart()): shell-exec the
    // named WM, fall back to re-execing ourselves.
    if (!restart_argv.empty()) {
      std::vector<char *> cargv;
      for (std::string &s : restart_argv) cargv.push_back(s.data());
      cargv.push_back(nullptr);
      execvp(cargv[0], cargv.data());
      std::perror("blackboxai: restart");    // exec failed - fall through to self
    }
    execvp(argv[0], argv);                   // re-exec ourselves (bare [restart])
    std::perror("blackboxai: restart self");
    return 1;
  }
  return 0;
}
