// tests/unit/util_test.cc
#include <doctest/doctest.h>
#include "Util.hh"

#include <cstdlib>

TEST_CASE("expandTilde") {
  setenv("HOME", "/home/tester", 1);
  CHECK(bt::expandTilde("~/.blackboxrc") == "/home/tester/.blackboxrc");
  CHECK(bt::expandTilde("~") == "/home/tester");            // bare tilde (reference crashed)
  CHECK(bt::expandTilde("/etc/bbrc") == "/etc/bbrc");       // absolute passes through
  CHECK(bt::expandTilde("rel/path") == "rel/path");         // relative passes through
  CHECK(bt::expandTilde("") == "");                          // empty (reference crashed)
  CHECK(bt::expandTilde("~user/x") == "/home/tester/x");     // classic behavior: '~anything' cuts at the first '/'
}

TEST_CASE("expandTilde without HOME returns the input") {
  unsetenv("HOME");
  CHECK(bt::expandTilde("~/x") == "~/x");
  setenv("HOME", "/home/tester", 1);  // restore for other tests in this binary
}
