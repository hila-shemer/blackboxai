// ext-session-lock-v1 lifecycle: accepts one lock client, blanks every output
// on the topmost scene layer, sends `locked` only after each head committed a
// post-blank frame (fallback timer bounds the wait), and keeps the session
// locked across a locker crash. Input gating lives in Server's funnels (they
// check locked() first); this class owns the protocol + scene state.
#ifndef BLACKBOXAI_SESSION_LOCK_HH
#define BLACKBOXAI_SESSION_LOCK_HH

#include "wlr.hpp"
#include "listener.hpp"
#include "Timer.hh"

#include <memory>
#include <vector>

namespace bbai {

  class Server;
  class Output;

  class SessionLock : public TimeoutHandler {
  public:
    SessionLock(Server &server, wlr_scene_tree *layer_lock);
    ~SessionLock();
    SessionLock(const SessionLock &) = delete;
    SessionLock &operator=(const SessionLock &) = delete;

    bool locked() const { return locked_; }

    void timeout() override;   // fallback deadline: send locked anyway

    // test introspection
    bool lockedSentForTest() const { return locked_sent_; }
    int blankRectCountForTest() const { return static_cast<int>(per_output_.size()); }
    bool hasActiveLockForTest() const { return lock_ != nullptr; }

  private:
    // One blank rect + post-blank commit tracking per head.
    struct PerOutput {
      Output *output = nullptr;
      wlr_scene_rect *blank = nullptr;
      bool committed = false;      // a post-blank frame reached this head
      bt::Listener commit;         // wlr_output.events.commit
    };

    void onNewLock(wlr_session_lock_v1 *lock);
    void onUnlock();
    void onLockDestroy();
    void blankOutput(Output *o);
    void maybeSendLocked();        // every head committed post-blank -> send once
    void sendLocked();
    void removeBlanks();

    Server &server_;
    wlr_scene_tree *layer_lock_;
    wlr_session_lock_manager_v1 *manager_ = nullptr;
    bt::Listener new_lock_;
    wlr_session_lock_v1 *lock_ = nullptr;   // live lock object (null once abandoned)
    bt::Listener lock_unlock_, lock_destroy_;
    bool locked_ = false;          // session state; survives a locker crash
    bool locked_sent_ = false;     // per-lock: wlroots asserts send_locked once
    std::vector<std::unique_ptr<PerOutput>> per_output_;
    Timer fallback_;               // headless: fires only via advanceClockForTest
  };

} // namespace bbai

#endif // BLACKBOXAI_SESSION_LOCK_HH
