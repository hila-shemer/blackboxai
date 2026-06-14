// B7: the injectable exec seam. The recording fake captures argv without
// spawning; the Posix runner's argv path is exercised by the menu test (B8) via
// the fake, never a real process in CI.
#include <doctest/doctest.h>
#include "CommandRunner.hh"

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
