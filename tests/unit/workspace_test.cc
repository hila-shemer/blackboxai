// A2: pure WorkspaceModel — count, current, default names, focus memory.
#include <doctest/doctest.h>
#include "Workspace.hh"

using namespace bbai;

TEST_CASE("WorkspaceModel defaults to 4 named workspaces on index 0") {
  WorkspaceModel m(4);
  CHECK(m.count() == 4);
  CHECK(m.current() == 0);
  CHECK(m.name(0) == "Workspace 1");
  CHECK(m.name(3) == "Workspace 4");
}

TEST_CASE("setCurrent + custom names") {
  WorkspaceModel m(3);
  m.setCurrent(2);
  CHECK(m.current() == 2);
  m.setName(1, "Web");
  CHECK(m.name(1) == "Web");
  m.setName(1, "");                 // empty resets to the default
  CHECK(m.name(1) == "Workspace 2");
}

TEST_CASE("per-workspace focus memory") {
  WorkspaceModel m(2);
  int a = 0, b = 0;                 // stand-ins for View* handles
  CHECK(m.focused(0) == nullptr);
  m.setFocused(0, &a);
  m.setFocused(1, &b);
  CHECK(m.focused(0) == &a);
  CHECK(m.focused(1) == &b);
  // clearFocused drops a handle from every workspace (window destroyed).
  m.clearFocused(&a);
  CHECK(m.focused(0) == nullptr);
  CHECK(m.focused(1) == &b);
}

TEST_CASE("add/remove workspaces, current clamps") {
  WorkspaceModel m(2);
  const unsigned idx = m.addWorkspace();
  CHECK(idx == 2);
  CHECK(m.count() == 3);
  CHECK(m.name(2) == "Workspace 3");

  m.setCurrent(2);
  m.removeLastWorkspace();          // current_ was the removed one -> clamps
  CHECK(m.count() == 2);
  CHECK(m.current() == 1);

  WorkspaceModel one(1);
  one.removeLastWorkspace();        // never below 1
  CHECK(one.count() == 1);
}

TEST_CASE("a count of 0 is clamped up to 1") {
  WorkspaceModel m(0);
  CHECK(m.count() == 1);
  CHECK(m.name(0) == "Workspace 1");
}
