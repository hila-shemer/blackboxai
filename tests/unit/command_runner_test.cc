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

TEST_CASE("PosixCommandRunner scrubs DISPLAY so X children can't escape to the host") {
  // We run no XWayland of our own: a child must not inherit our DISPLAY, or an
  // X11 app would connect to it (the host) and map outside us. Set one here and
  // prove the spawned shell sees it unset - ${DISPLAY-UNSET} distinguishes
  // unset from a leaked empty string.
  setenv("DISPLAY", ":99", 1);
  std::string path = "/tmp/bbai-cmddisplay-" + std::to_string(::getpid());
  ::unlink(path.c_str());

  PosixCommandRunner runner;
  runner.run({"/bin/sh", "-c", "printf '%s' \"${DISPLAY-UNSET}\" > " + path});

  std::string got;
  for (int i = 0; i < 200; ++i) {            // poll up to ~2s for the grandchild
    FILE *f = ::fopen(path.c_str(), "r");
    if (f) {
      char buf[64] = {0};
      size_t n = ::fread(buf, 1, sizeof buf - 1, f);
      ::fclose(f);
      if (n > 0) { got.assign(buf, n); break; }  // file may exist before it's written
    }
    struct timespec ts{0, 10 * 1000 * 1000};   // 10ms
    nanosleep(&ts, nullptr);
  }
  ::unlink(path.c_str());
  unsetenv("DISPLAY");
  CHECK(got == "UNSET");                        // scrubbed, not ":99"
}
