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

    wlr_surface *focusedLockSurface() const;   // first mapped lock surface, or null
    void handleNewOutput(Output *o);   // blank a head that appears mid-lock

    // test introspection
    bool lockedSentForTest() const { return locked_sent_; }
    int blankRectCountForTest() const { return static_cast<int>(per_output_.size()); }
    bool hasActiveLockForTest() const { return lock_ != nullptr; }
    int mappedLockSurfaceCountForTest() const;
    // Force the locked state without a real locker client, so a test can drive
    // the input gates that check locked(). Pairs with Server::lockForTest.
    void forceLockedForTest() { locked_ = true; }

  private:
    // One blank rect + post-blank commit tracking per head.
    struct PerOutput {
      Output *output = nullptr;
      wlr_scene_rect *blank = nullptr;
      bool committed = false;      // a post-blank frame reached this head
      bt::Listener commit;         // wlr_output.events.commit
      bt::Listener output_destroy; // hot-unplug: drop this entry + recount
    };

    // One client lock surface. The scene tree is wlroots-owned: it destroys
    // itself with the wlr_surface (subsurface_tree addon) - we only track it.
    struct SurfaceEntry {
      wlr_session_lock_surface_v1 *surface = nullptr;
      wlr_scene_tree *tree = nullptr;
      bt::Listener map, destroy;
    };

    void onNewSurface(wlr_session_lock_surface_v1 *ls);
    void enterKeyboard(wlr_surface *surface);

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
    bt::Listener lock_new_surface_;
    std::vector<std::unique_ptr<SurfaceEntry>> surfaces_;
    bool locked_ = false;          // session state; survives a locker crash
    bool locked_sent_ = false;     // per-lock: wlroots asserts send_locked once
    std::vector<std::unique_ptr<PerOutput>> per_output_;
    Timer fallback_;               // headless: fires only via advanceClockForTest
  };

} // namespace bbai

#endif // BLACKBOXAI_SESSION_LOCK_HH
