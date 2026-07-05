# ext-workspace-v1 Export Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Export BlackboxAI's `WorkspaceModel` over the `ext-workspace-v1` staging protocol - list, names, and active state out; a client ACTIVATE request in - so third-party Wayland panels/pagers can see and switch workspaces.

**Architecture:** wlroots 0.20 ships a complete server-side helper (`wlr_ext_workspace_v1.h`), so this is a thin bind, NOT a hand-rolled `wl_global`. We create one manager + one group spanning every output (classic Blackbox workspaces span all heads), and a private idempotent `Server::syncExtWorkspaces()` reconciles a handle vector against the model after every model mutation. ACTIVATE routes through the EXISTING `Server::setCurrentWorkspace` choke point - ext-workspace is a pure observer+forwarder, never a second source of truth for switching.

**Tech Stack:** C++20, wlroots 0.20, meson, wayland-server/-client, doctest. Build + test run inside the `blackboxai-ci:f44` container (this host has no wlroots 0.20).

## Global Constraints

- **Scope is ACTIVATE-only.** Advertise per-workspace caps = `ACTIVATE` only, group caps = `0`. Honor a client ACTIVATE by routing to `setCurrentWorkspace`. Ignore CREATE_WORKSPACE / REMOVE / ASSIGN / DEACTIVATE. Anything beyond this (create/remove via protocol) is a deliberate post-v1 add.
- **No hand-rolled protocol.** wlroots ships the helper + the generated enum header. NO `wl_global`, NO `wayland-scanner` server-side codegen, NO sanitize-shim (the header is C++-clean). The compositor's ONLY build change is one `#include` inside `toolkit/wlr.hpp`'s existing `extern "C"` block. Client-side XML codegen is added to `protocols/meson.build` for the TEST CLIENT ONLY.
- **One group, all outputs.** ONE `ext_workspace_group_handle_v1` spans every head. Stable id = the index string `"0".."3"`.
- **Route through the existing choke point.** No new public switching API. The ACTIVATE path calls `Server::setCurrentWorkspace(unsigned)` - the same function the menu WorkspaceSwitch action and Super+arrow keys already call.
- **Handle identity, not stashed indices.** Resolve an incoming ACTIVATE by scanning the handle vector for the pointer. Never stash a workspace index in `handle->data` - indices renumber on `removeLastWorkspace`.
- **Coverage gate >= 80%** (project sits ~92%). Every task runs its test; the final task runs the full suite + gcov.
- **Build/test only in `blackboxai-ci:f44`.** `rg`/file edits on the host; `meson`/`ninja`/`meson test` in the container.

## Land order + review

This slice lands **FIRST** in wave 3, before any doc/RPM/man-page work, and gets its **own adversarial review** before merge-train entry - it is the only wave-3 item with runtime C++, a client->Server activation path, a verified teardown-order abort trap, and an unverified destroy-ordering. It inserts lines into `Server.cc` near `:139/:268/:1768/:1800/:2112`, shifting everything below; the doc/RPM/man plans are authored AFTER this lands, against the final tree, so their `file:line` citations don't rot.

## Container preamble (run once per session before any build step)

All `meson`/`ninja`/`meson test` commands below run **inside** the container. The exact invocation (mount + image tag) is the same one waves 1-2 used. If a build dir does not yet exist, configure it once:

```bash
# host -> container shell (image blackboxai-ci:f44, repo mounted at /work)
meson setup build            # once, if build/ is absent
```

Then each "Run" step below is executed from the container's `/work`.

---

## File Structure

- `toolkit/wlr.hpp` - the single C-interop boundary. ADD one `#include` inside the `extern "C"` block. No other file includes the wlroots header directly.
- `src/Server.hh` - ADD four members (manager, group, handle vector, commit listener) + two private method decls (`syncExtWorkspaces`, `onExtWorkspaceCommit`) + a `const`-accessor for tests.
- `src/Server.cc` - create manager+group in the ctor; connect+disconnect the commit listener; `output_enter`/`output_leave`; call `syncExtWorkspaces()` from the model-mutation sites; implement `syncExtWorkspaces` + `onExtWorkspaceCommit`.
- `protocols/meson.build` - ADD the client-side ext-workspace XML to `client_protocols` (test client only).
- `tests/harness/ExtWorkspaceTestClient.{hh,cc}` - NEW opaque in-process client (mirrors `LockTestClient`), added to `harness_lib` sources in `tests/meson.build`.
- `tests/system/ext_workspace_test.cc` - NEW system test; registered in `tests/meson.build`.

---

### Task 1: Build seam - the extern-C include

**Files:**
- Modify: `toolkit/wlr.hpp:64-65` (inside the `extern "C"` block, next to `wlr_idle_notify_v1.h`)

**Interfaces:**
- Produces: the `wlr_ext_workspace_*` C symbols become linkable from any TU that includes `toolkit/wlr.hpp`.

The scout's POC proved (in the container) that a bare C++ include of this header compiles but **fails to link** - the header does not self-wrap in `extern "C"`, so C++ name-mangling produces 7 undefined references. Wrapping the include in `toolkit/wlr.hpp`'s existing `extern "C"` block links cleanly. `-DWLR_USE_UNSTABLE` is already set project-wide (`meson.build:5`), which this header `#error`s without. No sanitize shim: the header uses no `[static N]` array hints (unlike `wlr_scene.h`/`color.h`).

- [x] **Step 1: Re-verify the header exists in the container (the one fact this host cannot check)**

  VERIFIED in blackboxai-ci:f44. Header present. Signatures found (source of truth over the prose below - several differ):
  - `wlr_ext_workspace_manager_v1_create(display, uint32_t version)`
  - `wlr_ext_workspace_group_handle_v1_create(manager, uint32_t caps)` - takes caps (pass 0)
  - `wlr_ext_workspace_handle_v1_create(manager, const char *id, uint32_t caps)` - takes the **manager** (NOT the group) and caps at creation; there is **NO `set_capabilities`** function.
  - `wlr_ext_workspace_handle_v1_set_group(handle, group)` - attach a manager-created handle to the group.
  - `wlr_ext_workspace_handle_v1_set_coordinates(handle, const uint32_t *coords, size_t coords_len)` -> `(h, nullptr, 0)`
  - `wlr_ext_workspace_handle_v1_set_name(handle, const char *)`, `_set_active(handle, bool)`.
  - Commit event: `struct wlr_ext_workspace_v1_commit_event { struct wl_list *requests; }` (requests is a `wl_list *`).
  - Request: `struct wlr_ext_workspace_v1_request { enum ..._request_type type; struct wl_list link; union { ...; struct { wlr_ext_workspace_handle_v1 *workspace; } activate; ... }; }`.
  - Request enum tokens are **WLR_-prefixed**: `WLR_EXT_WORKSPACE_V1_REQUEST_ACTIVATE`.
  - Caps/state enum (wayland-protocols header, no WLR_ prefix): `EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE = 1`, `EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE = 1`.

Run (in container):
```bash
ls /usr/include/wlroots-0.20/wlr/types/wlr_ext_workspace_v1.h
grep -n 'wlr_ext_workspace_manager_v1_create\|wlr_ext_workspace_group_handle_v1_create\|wlr_ext_workspace_handle_v1_create\|events.commit\|_set_active\|_set_name\|_output_enter' \
  /usr/include/wlroots-0.20/wlr/types/wlr_ext_workspace_v1.h
```
Expected: the file exists; the grep prints the create/mutator/commit declarations. **Copy the exact signatures you see** - Tasks 2/3 cite them, and the container header is the source of truth over this plan's prose.

- [x] **Step 2: Add the include**

In `toolkit/wlr.hpp`, inside the `extern "C"` block, immediately after the idle-notify include at line 65:

```cpp
#include <wlr/types/wlr_idle_notify_v1.h>
// ext-workspace-v1 export (productize-v1 wave 3). wlroots ships the complete
// server-side helper; this one include is the whole compositor-side build
// change. NOT self-extern-C'd - it MUST live inside this block or the link
// fails with mangled wlr_ext_workspace_* symbols (verified in blackboxai-ci:f44).
#include <wlr/types/wlr_ext_workspace_v1.h>
```

- [x] **Step 3: Build to confirm still-green**

Run (in container):
```bash
ninja -C build
```
Expected: clean build, zero errors. This proves the header integrates with the real toolchain (the linkable-only-inside-extern-C claim).

- [x] **Step 4: Commit**

```bash
git add toolkit/wlr.hpp
git commit -m "ext-workspace: pull the wlroots helper header into the C-interop boundary

One include inside toolkit/wlr.hpp's extern \"C\" block is the whole
compositor-side build change - wlroots 0.20 ships the full server helper.
The header is not self-extern-C'd, so a bare C++ include links with mangled
wlr_ext_workspace_* symbols; inside the block it links clean.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Export path - manager, group, and the reconcile

**Files:**
- Modify: `src/Server.hh` (ADD members + method decls + a test accessor near `:87`/`:391`/`:415`)
- Modify: `src/Server.cc` - ctor near `:139` (create manager+group); `new_output` lambda `:275` (`output_enter`); `onOutputDestroyed` (`output_leave`); end of `applyConfig` (`:356`+); end of `setCurrentWorkspace` (`:1768`); end of `removeLastWorkspaceAndRehome` (`:1800`); menu `NewWorkspace` (`:2112`)
- Modify: `protocols/meson.build:8-14` (client stub)
- Create: `tests/harness/ExtWorkspaceTestClient.{hh,cc}`
- Modify: `tests/meson.build` (harness sources `:82-83` + register the test)
- Create: `tests/system/ext_workspace_test.cc`

**IMPLEMENTATION DEVIATIONS (container header is source of truth):**
- **Manager listener member order:** the generated `ext_workspace_manager_v1_listener` is `{ workspace_group, workspace, done, finished }` - the group callback comes FIRST. The plan's `s_mgr_listener` had `mgr_workspace, mgr_group` swapped; corrected in the impl.
- **`state` event is a `uint32_t` bitmask**, NOT a `wl_array`. The plan's `ws_state` (wl_array loop) was rewritten to `(state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE)`.
- **`wlr_ext_workspace_handle_v1_create(manager, id, caps)`** takes the MANAGER + caps (there is NO `set_capabilities`); attach to the group via `set_group(h, group)`. `set_coordinates(h, nullptr, 0)`.
- **`wlr_ext_workspace_group_handle_v1_create(mgr, caps)`** takes a caps arg (passed 0).
- **Teardown SIGSEGV (NEW trap, not in plan):** calling `output_leave` in `onOutputDestroyed` during full-display teardown races the group's own destruction (the display frees group + outputs) - SIGSEGV. Fixed by gating the `output_leave` behind `!tearing_down_` (moved below the existing teardown early-return). A live single-output removal still emits the leave.

**Interfaces:**
- Consumes: `WorkspaceModel::count()/current()/name(unsigned)` (`src/Workspace.hh:19-24`); `Server::workspaces()` returns `WorkspaceModel&` (`src/Server.hh:87`).
- Produces:
  - `void Server::syncExtWorkspaces();` - private, idempotent, early-returns if the manager is null (the ctor's `applyConfig()` at `:103` runs before the manager exists at `:139`). Reconciles `ext_ws_handles_` against `workspaces_`, then sets every handle's name + active bit. Call after every model size/active mutation.
  - Test-client accessors (Task-2 half): `int workspaceCount() const`, `std::string name(int i) const`, `int activeIndex() const`.

This is the pure-observer half: no commit listener yet, so the teardown assert-trap does not apply here (the manager's `display_destroy` handler asserts an *empty* commit-listener list, which it is until Task 3 connects one).

- [x] **Step 1: Write the failing test**

Create `tests/system/ext_workspace_test.cc`:

```cpp
// ext-workspace-v1 export: a real wayland-client binds ext_workspace_manager_v1,
// drains the workspace/group/name/state burst, and asserts our WorkspaceModel is
// mirrored out (count, names, exactly-one-active). Later cases drive an ACTIVATE
// request back in and mutate the model's size.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "HeadlessFixture.hh"
#include "ExtWorkspaceTestClient.hh"
#include "Server.hh"
#include "Workspace.hh"

#include <cstdlib>

using namespace bbai;

namespace {
  void bootOutput(Server &server) {
    for (int i = 0; i < 50 && server.activeSceneOutputForTest() == nullptr; ++i)
      server.dispatch();
    REQUIRE(server.activeSceneOutputForTest() != nullptr);
  }
  template <typename Cond, typename Pump>
  bool pumpUntil(Server &server, Cond cond, Pump pump, int iters = 1000) {
    for (int i = 0; i < iters && !cond(); ++i) { pump(); server.dispatch(); pump(); }
    return cond();
  }
}

TEST_CASE("ext-workspace mirrors the model out: 4 workspaces, names, one active") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootOutput(server);

  test::ExtWorkspaceTestClient c(server.socketName());
  REQUIRE(c.ok());

  bool ready = pumpUntil(server,
      [&] { return c.sawManager() && c.workspaceCount() == 4; },
      [&] { c.flush(); c.pump(); });
  CHECK(ready);
  CHECK(c.workspaceCount() == 4);
  CHECK(c.name(0) == "Workspace 1");   // WorkspaceModel default: "Workspace N", N=i+1
  CHECK(c.name(3) == "Workspace 4");
  CHECK(c.activeIndex() == static_cast<int>(server.workspaces().current()));
}
```

- [x] **Step 2: Add the client-side protocol stub**

In `protocols/meson.build`, add to `client_protocols` (the list at `:8-14`), after the `ext-idle-notify` line:

```meson
  protos_dir / 'staging' / 'ext-workspace' / 'ext-workspace-v1.xml',
```

That generates `ext-workspace-v1-client-protocol.h` + `-protocol.c` for the test client. The compositor needs NO server-side codegen here (wlroots ships the helper + `ext-workspace-v1-enum.h`).

- [x] **Step 3: Write the test-client header**

Create `tests/harness/ExtWorkspaceTestClient.hh`:

```cpp
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
```

- [x] **Step 4: Write the test-client implementation**

Create `tests/harness/ExtWorkspaceTestClient.cc`. **First** (in container) grep the generated client header for the exact request/event names - the protocol is stable but confirm before coding:
```bash
ninja -C build   # generates ext-workspace-v1-client-protocol.h
grep -n 'ext_workspace_handle_v1_listener\|ext_workspace_handle_v1_activate\|ext_workspace_manager_v1_listener\|_commit\|state\|\.name\|\.removed' \
  build/protocols/ext-workspace-v1-client-protocol.h | head -40
```
Then implement against what you saw (this code uses the protocol's canonical names - the `state` event carries a `wl_array` of `uint32` state values, `ACTIVE == 1` per `ext-workspace-v1-enum.h`):

```cpp
#include "ExtWorkspaceTestClient.hh"
#include "ConnectRetry.hh"

#include <wayland-client.h>
#include "ext-workspace-v1-client-protocol.h"

#include <cstring>
#include <poll.h>
#include <vector>

namespace bbai::test {

  namespace {
    void pumpDisplay(wl_display *d) {   // mirrors LockTestClient::pumpDisplay
      while (wl_display_prepare_read(d) != 0) wl_display_dispatch_pending(d);
      wl_display_flush(d);
      pollfd pfd = { wl_display_get_fd(d), POLLIN, 0 };
      if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) wl_display_read_events(d);
      else wl_display_cancel_read(d);
      wl_display_dispatch_pending(d);
    }
  }

  struct WsState {
    ext_workspace_handle_v1 *handle = nullptr;
    std::string name;
    bool active = false;
    bool removed = false;
  };

  struct ExtWorkspaceTestClient::Impl {
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    ext_workspace_manager_v1 *manager = nullptr;
    std::vector<WsState *> workspaces;   // creation order == our model index order

    WsState *find(ext_workspace_handle_v1 *h) {
      for (auto *w : workspaces) if (w->handle == h) return w;
      return nullptr;
    }
    // Live (non-removed) list in stable order.
    std::vector<WsState *> live() const {
      std::vector<WsState *> v;
      for (auto *w : workspaces) if (!w->removed) v.push_back(w);
      return v;
    }
  };

  // ---- workspace handle events ----
  static void ws_id(void *, ext_workspace_handle_v1 *, const char *) {}
  static void ws_name(void *data, ext_workspace_handle_v1 *, const char *name) {
    static_cast<WsState *>(data)->name = name ? name : "";
  }
  static void ws_coordinates(void *, ext_workspace_handle_v1 *, wl_array *) {}
  static void ws_state(void *data, ext_workspace_handle_v1 *, wl_array *state) {
    auto *w = static_cast<WsState *>(data);
    w->active = false;
    uint32_t *p;
    for (p = static_cast<uint32_t *>(state->data);
         reinterpret_cast<char *>(p) < static_cast<char *>(state->data) + state->size; ++p)
      if (*p == EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE) w->active = true;
  }
  static void ws_capabilities(void *, ext_workspace_handle_v1 *, uint32_t) {}
  static void ws_removed(void *data, ext_workspace_handle_v1 *) {
    static_cast<WsState *>(data)->removed = true;
  }
  static const ext_workspace_handle_v1_listener s_ws_listener = {
    ws_id, ws_name, ws_coordinates, ws_state, ws_capabilities, ws_removed };

  // ---- manager events ----
  static void mgr_workspace(void *data, ext_workspace_manager_v1 *,
                            ext_workspace_handle_v1 *handle) {
    auto *impl = static_cast<ExtWorkspaceTestClient::Impl *>(data);
    auto *w = new WsState();
    w->handle = handle;
    impl->workspaces.push_back(w);
    ext_workspace_handle_v1_add_listener(handle, &s_ws_listener, w);
  }
  static void mgr_group(void *, ext_workspace_manager_v1 *,
                        ext_workspace_group_handle_v1 *) {}   // one group; ignored
  static void mgr_done(void *, ext_workspace_manager_v1 *) {}
  static void mgr_finished(void *, ext_workspace_manager_v1 *) {}
  static const ext_workspace_manager_v1_listener s_mgr_listener = {
    mgr_workspace, mgr_group, mgr_done, mgr_finished };

  static void reg_global(void *data, wl_registry *reg, uint32_t name,
                         const char *iface, uint32_t) {
    auto *impl = static_cast<ExtWorkspaceTestClient::Impl *>(data);
    if (std::strcmp(iface, ext_workspace_manager_v1_interface.name) == 0) {
      impl->manager = static_cast<ext_workspace_manager_v1 *>(
        wl_registry_bind(reg, name, &ext_workspace_manager_v1_interface, 1));
      ext_workspace_manager_v1_add_listener(impl->manager, &s_mgr_listener, impl);
    }
  }
  static void reg_remove(void *, wl_registry *, uint32_t) {}
  static const wl_registry_listener s_reg_listener = { reg_global, reg_remove };

  ExtWorkspaceTestClient::ExtWorkspaceTestClient(const std::string &socket) {
    impl = new Impl();
    impl->display = connectWithRetry(socket.c_str());
    if (!impl->display) return;
    impl->registry = wl_display_get_registry(impl->display);
    wl_registry_add_listener(impl->registry, &s_reg_listener, impl);
    wl_display_flush(impl->display);
  }
  ExtWorkspaceTestClient::~ExtWorkspaceTestClient() {
    if (impl->display) {
      for (auto *w : impl->workspaces) {
        if (w->handle) ext_workspace_handle_v1_destroy(w->handle);
        delete w;
      }
      if (impl->manager) ext_workspace_manager_v1_destroy(impl->manager);
      wl_display_flush(impl->display);
      wl_display_disconnect(impl->display);
    }
    delete impl;
  }

  bool ExtWorkspaceTestClient::ok() const { return impl && impl->display; }
  void ExtWorkspaceTestClient::flush() { if (impl->display) wl_display_flush(impl->display); }
  void ExtWorkspaceTestClient::pump() { if (impl->display) pumpDisplay(impl->display); }

  bool ExtWorkspaceTestClient::sawManager() const { return impl->manager != nullptr; }
  int ExtWorkspaceTestClient::workspaceCount() const {
    return static_cast<int>(impl->live().size());
  }
  std::string ExtWorkspaceTestClient::name(int i) const {
    auto v = impl->live();
    return (i >= 0 && i < static_cast<int>(v.size())) ? v[i]->name : "";
  }
  int ExtWorkspaceTestClient::activeIndex() const {
    auto v = impl->live();
    for (int i = 0; i < static_cast<int>(v.size()); ++i) if (v[i]->active) return i;
    return -1;
  }

  // -- grown in Task 3 --
  void ExtWorkspaceTestClient::activate(int i) {
    auto v = impl->live();
    if (i < 0 || i >= static_cast<int>(v.size())) return;
    ext_workspace_handle_v1_activate(v[i]->handle);
    ext_workspace_manager_v1_commit(impl->manager);
    wl_display_flush(impl->display);
  }

} // namespace bbai::test
```

- [x] **Step 5: Register the harness client + the test**

In `tests/meson.build`, add `ExtWorkspaceTestClient.cc` to the `harness_lib` source list at `:82-83`:

```meson
harness_lib = static_library('harness',
  files('harness/HeadlessFixture.cc', 'harness/TestClient.cc',
        'harness/LockTestClient.cc', 'harness/ExtWorkspaceTestClient.cc'),
  dependencies : [bbai_dep, png_dep, client_protos_dep, wayland_client_dep],
  include_directories : include_directories('harness'))
```

And register the system test near the other lock-idle tests (after `:415`):

```meson
# ext-workspace-v1 export: bind the manager, drain the burst, assert the model
# is mirrored out; drive an ACTIVATE back in; mutate the model's size.
ext_workspace_exe = executable('ext-workspace-test',
  files('system/ext_workspace_test.cc'),
  dependencies : [harness_dep, doctest_dep])
test('ext_workspace', ext_workspace_exe, suite : 'system',
  workdir : meson.project_source_root(), env : test_env)
```

- [x] **Step 6: Run the test to verify it fails**

Run (in container):
```bash
meson test -C build ext_workspace -v
```
Expected: FAIL - `sawManager()` never turns true / `workspaceCount()` stays 0 (the compositor creates no manager yet).

- [x] **Step 7: Add the Server members**

In `src/Server.hh`, next to the `bt::Listener new_output;` member (`:391`) and the `idle_notifier_` member (`:415`):

```cpp
    // ext-workspace-v1 export (productize-v1 wave 3). ONE group spans every
    // output (classic workspaces are head-wide). syncExtWorkspaces() reconciles
    // the handle vector against workspaces_ after every model mutation; the
    // commit listener carries client ACTIVATE requests back in.
    wlr_ext_workspace_manager_v1 *ext_workspace_mgr_ = nullptr;
    wlr_ext_workspace_group_handle_v1 *ext_workspace_group_ = nullptr;
    std::vector<wlr_ext_workspace_handle_v1 *> ext_ws_handles_;
    bt::Listener ext_workspace_commit;
    void syncExtWorkspaces();
    void onExtWorkspaceCommit(void *data);
```

Add a test accessor next to the other `ForTest` accessors (near `:138`):

```cpp
    size_t extWorkspaceHandleCountForTest() const { return ext_ws_handles_.size(); }
```

- [x] **Step 8: Create the manager + group in the ctor**

In `src/Server.cc`, right after the idle-notifier line at `:139`:

```cpp
    idle_notifier_ = wlr_idle_notifier_v1_create(display);
    // ext-workspace-v1: one manager, one all-outputs group. Handles are NOT
    // built here - the model's final size/names are only known after the
    // primary output's applyConfig, so syncExtWorkspaces() populates them then
    // (and after every later mutation). The commit listener is connected in
    // Task 3; keeping it null here keeps the manager's display_destroy assert
    // (empty commit-listener list) satisfied.
    ext_workspace_mgr_ = wlr_ext_workspace_manager_v1_create(display, 1);
    ext_workspace_group_ = wlr_ext_workspace_group_handle_v1_create(ext_workspace_mgr_);
```

> Re-verify the `_group_handle_v1_create` arity against the container header from Task 1 Step 1 - the scout saw `create(mgr)`; if it takes a caps argument, pass `0` (group caps = 0).

- [x] **Step 9: Implement `syncExtWorkspaces`**

Add near `setCurrentWorkspace` in `src/Server.cc` (a natural home - same subsystem):

```cpp
  // Reconcile the ext-workspace handle vector against the model, then push
  // names + the active bit. Idempotent and cheap (a handful of workspaces).
  // Early-out until the manager exists: the ctor's applyConfig() at construction
  // runs before the manager is created, and this is called from applyConfig.
  void Server::syncExtWorkspaces() {
    if (!ext_workspace_mgr_) return;
    const unsigned n = workspaces_.count();

    // Grow: create missing tail handles, id = the index string, caps = ACTIVATE.
    while (ext_ws_handles_.size() < n) {
      const unsigned i = static_cast<unsigned>(ext_ws_handles_.size());
      wlr_ext_workspace_handle_v1 *h =
        wlr_ext_workspace_handle_v1_create(ext_workspace_group_, std::to_string(i).c_str());
      wlr_ext_workspace_handle_v1_set_capabilities(
        h, EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE);
      wlr_ext_workspace_handle_v1_set_coordinates(h, nullptr);   // positional; no coords
      ext_ws_handles_.push_back(h);
    }
    // Shrink: destroy surplus tail handles. The helper detaches the handle from
    // its group and emits `removed` internally - see Task 4's destroy-ordering
    // verification.
    while (ext_ws_handles_.size() > n) {
      wlr_ext_workspace_handle_v1_destroy(ext_ws_handles_.back());
      ext_ws_handles_.pop_back();
    }
    // Push current names + active bit. set_* batch on the manager's idle source;
    // the running event loop flushes a `done` - no manual flush.
    for (unsigned i = 0; i < n; ++i) {
      wlr_ext_workspace_handle_v1_set_name(ext_ws_handles_[i], workspaces_.name(i).c_str());
      wlr_ext_workspace_handle_v1_set_active(ext_ws_handles_[i], i == workspaces_.current());
    }
  }
```

> Re-verify `set_capabilities` / `set_coordinates` / `set_name` / `set_active` signatures against the container header. The scout saw `set_coordinates(h, &i, 1)` in its POC; passing empty coordinates is fine for positional workspaces - use whatever the header's signature is (a `wl_array`-style `(handle, coords, len)` takes `(h, nullptr, 0)`). Add `#include <string>` if not already transitively present in `Server.cc`.

- [x] **Step 10: Call `syncExtWorkspaces` from the mutation sites**

Four call sites in `src/Server.cc`:

1. End of `applyConfig` (`:356` def) - after the workspace grow/name block. Grows the vector on config; safe via the null-guard on the ctor's early `applyConfig()`.
2. End of `setCurrentWorkspace` - after `redrawWorkspaceLabel()` at `:1768`:
   ```cpp
       if (toolbar_) toolbar_->redrawWorkspaceLabel();
       syncExtWorkspaces();   // flip the active bit out to pagers
   ```
3. End of `removeLastWorkspaceAndRehome` - after `:1800`:
   ```cpp
       if (toolbar_) toolbar_->redrawWorkspaceLabel();
       syncExtWorkspaces();   // shrink the handle vector to match
   ```
4. Menu `NewWorkspace` action at `:2112` - after `addWorkspace()`:
   ```cpp
       case MenuItem::Act::NewWorkspace:    workspaces_.addWorkspace(); syncExtWorkspaces(); break;
   ```
   (`RemoveWorkspace` at `:2113` already reconciles via `removeLastWorkspaceAndRehome`.)

- [x] **Step 11: Wire the group's outputs**

In the `new_output` lambda (`src/Server.cc:275`, after `outputs_.push_back(o);`):

```cpp
      outputs_.push_back(o);
      if (ext_workspace_group_)
        wlr_ext_workspace_group_handle_v1_output_enter(ext_workspace_group_, wlr_out);
```

In `Server::onOutputDestroyed` (the server-side output-death path called from `Output`'s destroy listener at `src/Output.cc:53`), add the matching leave. Find `onOutputDestroyed` and, guarded on the group + the dying `wlr_output`, call:

```cpp
    if (ext_workspace_group_)
      wlr_ext_workspace_group_handle_v1_output_leave(ext_workspace_group_, dying_wlr_output);
```

> With one group spanning all outputs this is informational for pagers - switching works regardless. Wire it, but it is the lowest-risk piece if the `onOutputDestroyed` signature makes the `wlr_output*` awkward to reach; if so, skip the leave and note it in the commit body (the group outlives all outputs and dies with the display anyway).

- [x] **Step 12: Run the test to verify it passes**

Run (in container):
```bash
ninja -C build && meson test -C build ext_workspace -v
```
Expected: PASS - the client sees 4 workspaces, names `"Workspace 1".."Workspace 4"`, and `activeIndex()` matches `server.workspaces().current()`.

- [x] **Step 13: Commit**

```bash
git add src/Server.hh src/Server.cc protocols/meson.build \
  tests/harness/ExtWorkspaceTestClient.hh tests/harness/ExtWorkspaceTestClient.cc \
  tests/system/ext_workspace_test.cc tests/meson.build
git commit -m "ext-workspace: export the workspace model - manager, group, reconcile

One manager + one all-outputs group; a private idempotent syncExtWorkspaces()
reconciles the handle vector against WorkspaceModel and pushes names + the
active bit after every model mutation. Handles are populated after the primary
output's applyConfig (the model's final size is only known there), guarded so
the ctor's early applyConfig no-ops before the manager exists. Pure observer -
no client request path yet; that and its teardown trap are the next commit.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Activate-in path + the teardown trap

**Files:**
- Modify: `src/Server.cc` - ctor (connect the commit listener, next to the manager creation from Task 2); `~Server` at `:543` (disconnect); implement `onExtWorkspaceCommit`
- Modify: `tests/system/ext_workspace_test.cc` (ADD the activate case)

**Interfaces:**
- Consumes: `ext_workspace_mgr_->events.commit` (a `wlr_ext_workspace_v1_commit_event` carrying `struct wl_list *requests` of `wlr_ext_workspace_v1_request`, `.type` tagged; for `ACTIVATE`, `req->activate.workspace` is the `wlr_ext_workspace_handle_v1*`). `Server::setCurrentWorkspace(unsigned)` (`:1745`).
- Consumes (test): `ExtWorkspaceTestClient::activate(int)` (added in Task 2 Step 4).

**The teardown trap (POC-verified, load-bearing):** the manager's `display_destroy` handler asserts `wl_list_empty(&manager->events.commit.listener_list)` (`types/wlr_ext_workspace_v1.c:511`). Once the commit listener is connected, it MUST be disconnected before `wl_display_destroy` - the same class of trap as `new_output`, handled at the same spot (`:543`). This is why the listener and its disconnect land in ONE task: connecting it without the disconnect aborts every test at shutdown.

- [x] **Step 1: Write the failing test (ADD to `ext_workspace_test.cc`)**

```cpp
TEST_CASE("client ACTIVATE routes through setCurrentWorkspace") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootOutput(server);

  test::ExtWorkspaceTestClient c(server.socketName());
  REQUIRE(c.ok());
  REQUIRE(pumpUntil(server, [&] { return c.workspaceCount() == 4; },
                    [&] { c.flush(); c.pump(); }));
  REQUIRE(server.workspaces().current() == 0);   // boots on workspace 0

  c.activate(2);
  bool moved = pumpUntil(server,
      [&] { return server.workspaces().current() == 2; },
      [&] { c.flush(); c.pump(); });
  CHECK(moved);
  CHECK(server.workspaces().current() == 2);
  // The active bit is mirrored back out to the client.
  bool mirrored = pumpUntil(server, [&] { return c.activeIndex() == 2; },
                            [&] { c.flush(); c.pump(); });
  CHECK(mirrored);
}
```

- [x] **Step 2: Run to verify it fails**

Run (in container):
```bash
meson test -C build ext_workspace -v
```
Expected: FAIL - `current()` stays 0 (no commit listener honors the request yet).

- [x] **Step 3: Connect the commit listener + implement the handler**

In the ctor (`src/Server.cc`, right after the group creation from Task 2):

```cpp
    ext_workspace_group_ = wlr_ext_workspace_group_handle_v1_create(ext_workspace_mgr_);
    ext_workspace_commit.connect(&ext_workspace_mgr_->events.commit,
                                 [this](void *data) { onExtWorkspaceCommit(data); });
```

Implement the handler next to `syncExtWorkspaces` (`src/Server.cc`):

```cpp
  // A client committed a batch of requests. We advertise ACTIVATE only, so
  // honor ACTIVATE and ignore the rest. Resolve the target by SCANNING the
  // handle vector for the pointer - never by a stashed index (indices renumber
  // on removeLastWorkspace). setCurrentWorkspace is the same choke point the
  // menu + Super+arrow keys use; ext-workspace never becomes a second source
  // of truth for switching.
  void Server::onExtWorkspaceCommit(void *data) {
    auto *ev = static_cast<wlr_ext_workspace_v1_commit_event *>(data);
    wlr_ext_workspace_v1_request *req;
    wl_list_for_each(req, ev->requests, link) {
      if (req->type != EXT_WORKSPACE_V1_REQUEST_ACTIVATE) continue;
      wlr_ext_workspace_handle_v1 *target = req->activate.workspace;
      if (!target) continue;   // protocol nulls the field if the handle died
      for (unsigned i = 0; i < ext_ws_handles_.size(); ++i)
        if (ext_ws_handles_[i] == target) { setCurrentWorkspace(i); break; }
    }
  }
```

> Re-verify the event struct name, the `requests` list field, the `link` member name, the request `.type` enum token, and `req->activate.workspace` against the container header (Task 1 Step 1's grep, extended to `commit_event`/`request`). The scout read these from the header but did NOT verify the enum-token spelling against source - grep `EXT_WORKSPACE_V1_REQUEST` in the container's `wlr/types/wlr_ext_workspace_v1.h` before trusting the token above.

- [x] **Step 4: Disconnect on teardown**

In `~Server` (`src/Server.cc:543`), next to `new_output.disconnect()`:

```cpp
    new_output.disconnect();
    ext_workspace_commit.disconnect();   // manager's display_destroy asserts an
                                         // empty commit-listener list; skip this
                                         // and shutdown aborts (POC-verified).
```

- [x] **Step 5: Run to verify it passes**

Run (in container):
```bash
ninja -C build && meson test -C build ext_workspace -v
```
Expected: PASS - `current()` moves to 2, `activeIndex()` mirrors to 2, and the process exits cleanly (no `wl_list_empty` abort at teardown).

- [x] **Step 6: Commit**

```bash
git add src/Server.cc tests/system/ext_workspace_test.cc
git commit -m "ext-workspace: honor client ACTIVATE via the existing switch choke point

The commit listener resolves an ACTIVATE by scanning the handle vector for the
pointer (indices renumber, so never stash one) and calls setCurrentWorkspace -
the same path the menu and Super+arrow keys use. ext-workspace stays a pure
forwarder, not a second source of truth. The listener MUST disconnect before
wl_display_destroy: the manager's display_destroy handler asserts an empty
commit-listener list, and skipping it aborts shutdown (POC-verified).

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Model-size mutation reconcile + destroy-ordering

**Files:**
- Modify: `tests/system/ext_workspace_test.cc` (ADD the grow/shrink case)

**Interfaces:**
- Consumes: `WorkspaceModel::addWorkspace()` (`src/Workspace.hh:32`), `Server::removeLastWorkspaceAndRehome()` (`src/Server.cc:1771`), `Server::syncExtWorkspaces()` (called from those; the test drives them via the public model + a switch). `extWorkspaceHandleCountForTest()` (Task 2 Step 7).

This task adds no product code - `syncExtWorkspaces` already grows and shrinks. It exists as its own reviewer gate because the **destroy-ordering is unverified against wlroots source** (the scout read only the header): does `wlr_ext_workspace_handle_v1_destroy` detach the handle from its group internally, or must we `set_group(NULL)` first? The test drives add-then-remove through a real client and asserts no abort - the empirical answer. If it aborts on destroy, the fix is a `set_group(h, nullptr)` (or the header's equivalent) before `_destroy` in `syncExtWorkspaces`'s shrink loop; re-run until green and note the finding in the commit body.

**DESTROY-ORDERING FINDING (empirical, container run):** the grow/shrink test passes with a clean exit and NO abort/segfault. `wlr_ext_workspace_handle_v1_destroy` detaches the handle from its group internally - **no explicit `set_group(NULL)` was needed** before `_destroy`. The plain `_destroy` in the shrink loop is correct.

- [x] **Step 1: Write the failing test (ADD to `ext_workspace_test.cc`)**

```cpp
TEST_CASE("adding and removing a workspace reconciles the handle vector") {
  setenv("WLR_BACKENDS", "headless", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  Server server(/*headless=*/true);
  REQUIRE(server.ok());
  bootOutput(server);

  test::ExtWorkspaceTestClient c(server.socketName());
  REQUIRE(c.ok());
  REQUIRE(pumpUntil(server, [&] { return c.workspaceCount() == 4; },
                    [&] { c.flush(); c.pump(); }));

  // Grow: a 5th workspace appears out to the client.
  server.workspaces().addWorkspace();
  server.syncExtWorkspacesForTest();   // see Step 2
  CHECK(server.extWorkspaceHandleCountForTest() == 5);
  bool grew = pumpUntil(server, [&] { return c.workspaceCount() == 5; },
                        [&] { c.flush(); c.pump(); });
  CHECK(grew);
  CHECK(c.name(4) == "Workspace 5");

  // Shrink: removeLastWorkspaceAndRehome pops the tail; the client sees `removed`.
  server.removeLastWorkspaceAndRehome();
  CHECK(server.extWorkspaceHandleCountForTest() == 4);
  bool shrank = pumpUntil(server, [&] { return c.workspaceCount() == 4; },
                          [&] { c.flush(); c.pump(); });
  CHECK(shrank);
  // No abort reaching here == destroy path is group-detach-clean.
}
```

- [x] **Step 2: Add a test hook for the direct-grow case**

`addWorkspace()` on the model alone does not call `syncExtWorkspaces` (only the menu action does). Expose a thin test hook in `src/Server.hh` next to the other `ForTest` methods (near `:77`):

```cpp
    void syncExtWorkspacesForTest() { syncExtWorkspaces(); }
```

(The shrink half needs no hook - `removeLastWorkspaceAndRehome` reconciles on its own.)

- [x] **Step 3: Run to verify it passes (or catches the destroy-ordering trap)**

Run (in container):
```bash
ninja -C build && meson test -C build ext_workspace -v
```
Expected: PASS. **If it aborts on the shrink** (`wlr_ext_workspace_handle_v1_destroy` asserting a group-attached handle), add before the `_destroy` call in `syncExtWorkspaces`'s shrink loop:
```cpp
      wlr_ext_workspace_handle_v1_set_group(ext_ws_handles_.back(), nullptr);
```
then re-run to green. Record which path was needed in the commit body.

- [x] **Step 4: Commit**

```bash
git add src/Server.hh tests/system/ext_workspace_test.cc
git commit -m "ext-workspace: reconcile the handle vector on workspace add/remove

Drives an add-then-remove through a real client and asserts the handle vector
tracks the model both ways - and, more to the point, that the shrink's
handle_v1_destroy does not abort on a still-grouped handle. [Record here:
destroy detaches internally / needed an explicit set_group(NULL) first.]

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Full-suite green + coverage gate + review prep

**Files:** none (verification only)

**Interfaces:** none.

This slice must not regress the suite or drop coverage below the 80% gate (project ~92%), and it enters its own adversarial review next. This task is the gate.

- [x] **Step 1: Run the whole suite**

Run (in container):
```bash
meson test -C build
```
Expected: all tests PASS, including the three new `ext_workspace` cases and every prior wave-1/wave-2 test (proves the `Server.cc` insertions near `:139/:268/:1768/:1800/:2112` broke nothing below them).

- [x] **Step 2: Coverage gate**

Run (in container) the project's usual gcov flow (same one waves 1-2 used - configure a coverage build dir if absent):
```bash
meson setup build-cov -Db_coverage=true 2>/dev/null || true
meson test -C build-cov
ninja -C build-cov coverage
```
Expected: overall line coverage >= 80% (project ~92%). Confirm `Server.cc`'s new `syncExtWorkspaces`/`onExtWorkspaceCommit` lines are exercised (the three cases hit grow, shrink, active-flip, and the ACTIVATE route). If the `onOutputDestroyed` `output_leave` line (Task 2 Step 11) is uncovered and you kept it, that single informational line is acceptable - note it.

- [x] **Step 3: Self-review against scope**

Confirm, by reading the final diff:
- No `wl_global` hand-roll, no `wayland-scanner` server-side codegen, no sanitize-shim (the one build change is the `toolkit/wlr.hpp` include).
- Per-workspace caps = `ACTIVATE` only; group caps = 0; CREATE/REMOVE/ASSIGN/DEACTIVATE ignored.
- ACTIVATE resolves by pointer-scan, not a stashed index.
- `ext_workspace_commit.disconnect()` is present in `~Server` before `wl_display_destroy`.

- [x] **Step 4: Hand off to adversarial review**

The slice is complete and green. Per the wave-3 program decisions it gets its OWN adversarial review before merge-train entry - flag it for that review (do not merge yet). Reviewer focus areas: the destroy-ordering finding from Task 4, the teardown disconnect, the pointer-scan ACTIVATE resolution, and the container-verified header signatures (`set_coordinates`/`set_capabilities`/`create` arity, the `commit_event`/`request` struct shape).

**GATE RESULT (blackboxai-ci:f44, coverage build-f44):**
- Full suite: 76/76 tests pass. One flake on the first parallel run (`gray_window` `REQUIRE(mapped())`, the known cold-cache connect race) - re-ran alone and passed. `ext_workspace` = 3 cases / 25 assertions, clean exit.
- Coverage: overall 92% line (`gcovr --fail-under-line=80` exit 0). Every new ext-workspace line in `Server.cc` is exercised, INCLUDING the `output_leave` (the live single-output-removal path is hit by an existing multihead test) - no uncovered new lines. `Server.cc` file total 85% (its pre-existing D-Bus/error-path baseline; the new code is fully covered).
- Scope: no `wl_global`/`wayland-scanner` server codegen/`wl_resource_create` in `src/` (grep-clean); `protocols/meson.build` adds ONE client-side XML line only; caps = ACTIVATE-only at create + group caps 0; ACTIVATE resolves by pointer-scan; `ext_workspace_commit.disconnect()` present in `~Server` before `wl_display_destroy`.

---

## Self-Review (plan author's checklist, completed)

**Spec coverage** - every scout/synthesis contract is a task: build seam (T1), export path + one-group + reconcile + output_enter/leave (T2), ACTIVATE-in through `setCurrentWorkspace` + teardown-trap disconnect (T3), size-mutation reconcile + destroy-ordering verification (T4), coverage gate + review handoff (T5). The five seam contracts (no new switching API; private reconcile; teardown disconnect; caps=ACTIVATE-only/one-group; one-line build change) each map to a task.

**Type consistency** - `syncExtWorkspaces()`/`onExtWorkspaceCommit(void*)` declared in T2 Step 7, defined T2 Step 9 / T3 Step 3, called from the four T2 Step 10 sites + T3 ctor. `ext_ws_handles_` (the vector) is the single identity source used by both. Test-client surface (`sawManager`/`workspaceCount`/`name`/`activeIndex`/`activate`) is declared once in T2 Step 3 and used verbatim in T2/T3/T4 tests. `extWorkspaceHandleCountForTest`/`syncExtWorkspacesForTest` declared in T2 Step 7 / T4 Step 2.

**Container-only facts flagged for re-verification at implement time** (this host has no wlroots 0.20): every wlroots symbol signature (`_group_handle_v1_create` arity, `set_coordinates`/`set_capabilities` shapes, the `commit_event`/`request` struct + enum tokens, the generated client-side listener member order). T1 Step 1 and the inline `>` notes make the header the source of truth over this plan's prose. The teardown-trap and destroy-ordering are exercised by real tests (T3/T4), not asserted on faith.
