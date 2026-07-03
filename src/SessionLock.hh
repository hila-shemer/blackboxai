// ext-session-lock-v1 lifecycle: accepts one lock client, blanks every output
// on the topmost scene layer, sends `locked` only after each head committed a
// post-blank frame (fallback timer bounds the wait), and keeps the session
// locked across a locker crash. Input gating lives in Server's funnels (they
// check locked() first); this class owns the protocol + scene state.
#ifndef BLACKBOXAI_SESSION_LOCK_HH
#define BLACKBOXAI_SESSION_LOCK_HH

#include "wlr.hpp"
#include "listener.hpp"

namespace bbai {

  class Server;

  class SessionLock {
  public:
    SessionLock(Server &server, wlr_scene_tree *layer_lock);
    ~SessionLock();
    SessionLock(const SessionLock &) = delete;
    SessionLock &operator=(const SessionLock &) = delete;

    bool locked() const { return locked_; }

  private:
    Server &server_;
    wlr_scene_tree *layer_lock_;
    wlr_session_lock_manager_v1 *manager_ = nullptr;
    bt::Listener new_lock_;
    bool locked_ = false;
  };

} // namespace bbai

#endif // BLACKBOXAI_SESSION_LOCK_HH
