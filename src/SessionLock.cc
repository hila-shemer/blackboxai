#include "SessionLock.hh"
#include "Server.hh"
#include "Output.hh"

namespace bbai {

  // Fallback deadline for the send_locked wait: the protocol wants a blanked
  // frame PRESENTED on every head first, but also demands a time limit so a
  // stalled head can't wedge the lock (the suspend race). One second is
  // generous - heads commit within a frame or two.
  static constexpr int64_t kLockedFallbackMs = 1000;

  SessionLock::SessionLock(Server &server, wlr_scene_tree *layer_lock)
    : server_(server), layer_lock_(layer_lock),
      fallback_(server.timerRegistry(), *this) {
    manager_ = wlr_session_lock_manager_v1_create(server_.display);
    new_lock_.connect(&manager_->events.new_lock, [this](void *data) {
      onNewLock(static_cast<wlr_session_lock_v1 *>(data));
    });
  }

  SessionLock::~SessionLock() {
    // Listeners disconnect via RAII (per_output_ entries carry theirs). The
    // blanks are ours to destroy; the manager global dies with the display.
    removeBlanks();
  }

  void SessionLock::onNewLock(wlr_session_lock_v1 *lock) {
    if (lock_) {
      // An active locker already holds the session: deny. wlroots sends
      // `finished` and frees the resource - the client never hangs.
      wlr_session_lock_v1_destroy(lock);
      return;
    }
    lock_ = lock;
    locked_sent_ = false;
    locked_ = true;

    lock_new_surface_.connect(&lock->events.new_surface, [this](void *data) {
      onNewSurface(static_cast<wlr_session_lock_surface_v1 *>(data));
    });
    lock_unlock_.connect(&lock->events.unlock, [this](void *) { onUnlock(); });
    lock_destroy_.connect(&lock->events.destroy, [this](void *) { onLockDestroy(); });

    server_.handleSessionLocked();   // park focus; modal aborts arrive in Task 7

    for (Output *o : server_.outputs_)
      blankOutput(o);

    fallback_.start(kLockedFallbackMs, /*recurring=*/false);
    maybeSendLocked();   // zero-output edge: nothing to wait for
  }

  void SessionLock::blankOutput(Output *o) {
    auto po = std::make_unique<PerOutput>();
    po->output = o;
    wlr_box box;
    wlr_output_layout_get_box(server_.output_layout, o->wlrOutput(), &box);
    static const float black[4] = { 0.f, 0.f, 0.f, 1.f };
    po->blank = wlr_scene_rect_create(layer_lock_, box.width, box.height, black);
    wlr_scene_node_set_position(&po->blank->node, box.x, box.y);
    // Any commit from here on carries the blanked scene: the rect damaged the
    // whole head, so the next frame the scene commits includes it.
    po->commit.connect(&o->wlrOutput()->events.commit, [this, p = po.get()](void *) {
      if (!p->committed) {
        p->committed = true;
        maybeSendLocked();
      }
    });
    per_output_.push_back(std::move(po));
    o->scheduleFrame();   // don't wait for organic damage-driven scheduling
  }

  void SessionLock::maybeSendLocked() {
    if (!lock_ || locked_sent_) return;
    for (const auto &po : per_output_)
      if (!po->committed) return;
    sendLocked();
  }

  void SessionLock::sendLocked() {
    if (!lock_ || locked_sent_) return;   // wlroots asserts once-only
    wlr_session_lock_v1_send_locked(lock_);
    locked_sent_ = true;
    fallback_.stop();
  }

  void SessionLock::timeout() { sendLocked(); }

  void SessionLock::onUnlock() {
    locked_ = false;
    removeBlanks();
    fallback_.stop();
    // Focus restore (Server::handleSessionUnlocked) arrives in Task 6.
  }

  void SessionLock::onLockDestroy() {
    lock_ = nullptr;
    lock_unlock_.disconnect();
    lock_destroy_.disconnect();
    lock_new_surface_.disconnect();
    surfaces_.clear();   // wlroots already tore the lock surfaces down before this signal
    fallback_.stop();
    // locked_ stays as-is on purpose: destroy WITHOUT unlock is an abandoned
    // lock (locker crashed) - the session MUST remain locked, blanks and all.
    // A new locker may take over (Task 8).
  }

  void SessionLock::onNewSurface(wlr_session_lock_surface_v1 *ls) {
    auto e = std::make_unique<SurfaceEntry>();
    e->surface = ls;
    wlr_box box;
    wlr_output_layout_get_box(server_.output_layout, ls->output, &box);
    wlr_session_lock_surface_v1_configure(
      ls, static_cast<uint32_t>(box.width), static_cast<uint32_t>(box.height));
    // Created after the blanks, so within layer_lock the surface tree stacks
    // above them; an early-destroyed surface falls back to the blank beneath
    // (the xml's solid-color obligation) with zero extra work.
    e->tree = wlr_scene_subsurface_tree_create(layer_lock_, ls->surface);
    wlr_scene_node_set_position(&e->tree->node, box.x, box.y);
    e->map.connect(&ls->surface->events.map, [this, s = ls->surface](void *) {
      // First mapped lock surface takes the keyboard (spec-suggested policy).
      if (server_.seat->keyboard_state.focused_surface == nullptr)
        enterKeyboard(s);
    });
    e->destroy.connect(&ls->events.destroy, [this, raw = e.get()](void *) {
      // Drop our record ONLY. wlroots unmaps the surface now and the
      // subsurface tree destroys itself when the wlr_surface dies -
      // destroying it here would double-free (subsurface_tree.c:150-154).
      wlr_surface *dead = raw->surface->surface;
      std::erase_if(surfaces_, [raw](const std::unique_ptr<SurfaceEntry> &p) {
        return p.get() == raw;
      });
      if (locked_ && server_.seat->keyboard_state.focused_surface == dead) {
        wlr_seat_keyboard_notify_clear_focus(server_.seat);
        if (wlr_surface *next = focusedLockSurface())
          enterKeyboard(next);   // hand the keyboard to a surviving lock surface
      }
    });
    surfaces_.push_back(std::move(e));
  }

  void SessionLock::enterKeyboard(wlr_surface *surface) {
    // Headless has no wlr_keyboard; notify_enter copes with null keycodes and
    // still sets keyboard_state.focused_surface (the part input routing needs).
    if (wlr_keyboard *kb = wlr_seat_get_keyboard(server_.seat))
      wlr_seat_keyboard_notify_enter(server_.seat, surface, kb->keycodes,
                                     kb->num_keycodes, &kb->modifiers);
    else
      wlr_seat_keyboard_notify_enter(server_.seat, surface, nullptr, 0, nullptr);
  }

  wlr_surface *SessionLock::focusedLockSurface() const {
    for (const auto &e : surfaces_)
      if (e->surface->surface->mapped) return e->surface->surface;
    return nullptr;
  }

  void SessionLock::removeBlanks() {
    for (auto &po : per_output_)
      if (po->blank) wlr_scene_node_destroy(&po->blank->node);
    per_output_.clear();   // drops the commit listeners too
  }

} // namespace bbai
