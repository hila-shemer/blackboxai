#include "CommandRunner.hh"

#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>

namespace bbai {

  void PosixCommandRunner::run(const std::vector<std::string> &argv) {
    if (argv.empty()) return;
    pid_t pid = fork();
    if (pid == 0) {
      // Child: double-fork + setsid so the grandchild is reparented to init and
      // doesn't become a zombie or get our controlling terminal.
      setsid();
      if (fork() == 0) {
        if (!wayland_display_.empty())
          setenv("WAYLAND_DISPLAY", wayland_display_.c_str(), 1);
        // Drop the host's DISPLAY: we run no XWayland of our own, so an X11
        // child (xterm, old GTK/Qt) would otherwise connect to whatever X
        // server we inherited - i.e. escape onto the host desktop. Drop it so
        // they fail where you can see them. When XWayland lands this becomes a
        // setenv to our own :N.
        unsetenv("DISPLAY");
        std::vector<char *> args;
        args.reserve(argv.size() + 1);
        for (const std::string &s : argv) args.push_back(const_cast<char *>(s.c_str()));
        args.push_back(nullptr);
        execvp(args[0], args.data());
        _exit(127);  // exec failed
      }
      _exit(0);
    } else if (pid > 0) {
      waitpid(pid, nullptr, 0);  // reap the intermediate child
    }
  }

} // namespace bbai
