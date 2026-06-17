#include <doctest/doctest.h>
#include "Rootmenu.hh"
#include "Workspace.hh"
#include "Text.hh"
using namespace bbai;
TEST_CASE("buildWorkspacesSubmenu lists switch rows + New/Remove") {
  WorkspaceModel ws;                                   // default workspaces
  auto sub = rootmenu::buildWorkspacesSubmenu(ws);
  REQUIRE(sub.size() == ws.count() + 3);               // N switch rows + sep + New + Remove
  for (unsigned i = 0; i < ws.count(); ++i) {
    CHECK(sub[i].action == MenuItem::Act::WorkspaceSwitch);
    CHECK(sub[i].workspace == i);
  }
  CHECK(sub[ws.current()].checked == true);
  CHECK(sub[ws.count()].kind == MenuItem::Kind::Separator);
  CHECK(sub[ws.count()+1].action == MenuItem::Act::NewWorkspace);
  CHECK(sub[ws.count()+2].action == MenuItem::Act::RemoveWorkspace);
}
