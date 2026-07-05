// In-process ext-workspace-v1 client. OPAQUE like LockTestClient - no
// wayland-client.h here, so it coexists with the server in one test binary.
// Drive with the usual flush(); server.dispatch(); pump(); loop.
#ifndef BLACKBOXAI_EXT_WORKSPACE_TEST_CLIENT_HH
#define BLACKBOXAI_EXT_WORKSPACE_TEST_CLIENT_HH

#include <string>

namespace bbai::test {

  class ExtWorkspaceTestClient {
  public:
    explicit ExtWorkspaceTestClient(const std::string &socket);
    ~ExtWorkspaceTestClient();
    ExtWorkspaceTestClient(const ExtWorkspaceTestClient &) = delete;
    ExtWorkspaceTestClient &operator=(const ExtWorkspaceTestClient &) = delete;

    bool ok() const;
    void flush();
    void pump();

    bool sawManager() const;         // ext_workspace_manager_v1 advertised + bound
    int  workspaceCount() const;     // workspace handles seen (minus removed)
    std::string name(int i) const;   // -> "" if out of range or no name yet
    int  activeIndex() const;        // index of the ACTIVE workspace, -1 if none

    // -- grown in Task 3 --
    void activate(int i);            // ext_workspace_handle_v1.activate + commit

    struct Impl;
  private:
    Impl *impl;
  };

} // namespace bbai::test
#endif
