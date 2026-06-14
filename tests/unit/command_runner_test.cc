// B7: the injectable exec seam. The recording fake captures argv without
// spawning; the Posix runner's argv path is exercised by the menu test (B8) via
// the fake, never a real process in CI.
#include <doctest/doctest.h>
#include "CommandRunner.hh"

#include <cstdio>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

using namespace bbai;

TEST_CASE("FakeCommandRunner records argv without spawning") {
  FakeCommandRunner r;
  CHECK(r.runCount() == 0);
  r.run({"foot"});
  CHECK(r.runCount() == 1);
  CHECK(r.lastCommand() == std::vector<std::string>{"foot"});
  r.run({"xterm", "-e", "vi"});
  CHECK(r.runCount() == 2);
  CHECK(r.lastCommand() == std::vector<std::string>{"xterm", "-e", "vi"});
}

TEST_CASE("the abstract interface dispatches polymorphically") {
  FakeCommandRunner fake;
  CommandRunner &runner = fake;     // used the same way the Server holds it
  runner.run({"xcalc"});
  CHECK(fake.lastCommand() == std::vector<std::string>{"xcalc"});
}

TEST_CASE("PosixCommandRunner actually double-fork/execs the argv") {
  // Unique marker file the spawned grandchild creates; proves the real
  // fork/setsid/execvp path runs (no real GUI app, just /bin/sh touching a file).
  std::string path = "/tmp/bbai-cmdrunner-" + std::to_string(::getpid());
  ::unlink(path.c_str());

  PosixCommandRunner runner;
  runner.run({"/bin/sh", "-c", "printf '' > " + path});

  bool created = false;
  for (int i = 0; i < 200; ++i) {            // poll up to ~2s for the detached grandchild
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) { created = true; break; }
    struct timespec ts{0, 10 * 1000 * 1000};  // 10ms
    nanosleep(&ts, nullptr);
  }
  CHECK(created);
  ::unlink(path.c_str());

  // Empty argv is a no-op (must not spawn / crash).
  runner.run({});
}
