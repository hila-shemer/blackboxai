#include "SessionLock.hh"
#include "Server.hh"
#include "Output.hh"

namespace bbai {

  SessionLock::SessionLock(Server &server, wlr_scene_tree *layer_lock)
    : server_(server), layer_lock_(layer_lock) {
    manager_ = wlr_session_lock_manager_v1_create(server_.display);
    new_lock_.connect(&manager_->events.new_lock, [this](void *data) {
      // Accept/deny arrives in Task 3; until then every lock is denied so a
      // client never hangs waiting for locked/finished.
      wlr_session_lock_v1_destroy(static_cast<wlr_session_lock_v1 *>(data));
    });
  }

  SessionLock::~SessionLock() {
    // Listeners disconnect via RAII; the manager global dies with the display.
  }

} // namespace bbai
