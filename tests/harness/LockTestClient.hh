// In-process ext-session-lock-v1 client + ext-idle-notify-v1 client (one TU).
// Deliberately OPAQUE like TestClient - no wayland-client.h in the header, so
// they coexist with the server side in one test binary. Drive with the usual
// flush(); server.dispatch(); pump(); loop.
#ifndef BLACKBOXAI_LOCK_TEST_CLIENT_HH
#define BLACKBOXAI_LOCK_TEST_CLIENT_HH

#include <string>
#include <cstdint>

namespace bbai::test {

  class LockTestClient {
  public:
    explicit LockTestClient(const std::string &socket);
    ~LockTestClient();
    LockTestClient(const LockTestClient &) = delete;
    LockTestClient &operator=(const LockTestClient &) = delete;

    bool ok() const;
    void flush();
    void pump();

    int outputCount() const;        // wl_output globals seen (and bound)
    bool sawLockManager() const;    // ext_session_lock_manager_v1 advertised
    bool sawIdleNotifier() const;   // ext_idle_notifier_v1 advertised

    // -- grown in later tasks --
    void lock();                    // ext_session_lock_manager_v1.lock
    bool lockedReceived() const;
    bool finishedReceived() const;
    void createLockSurface(int output_index, uint32_t argb);
    int configuredWidth(int output_index) const;   // -1 until configured
    int configuredHeight(int output_index) const;
    void unlockAndDestroy();        // the clean unlock path

    struct Impl;

  private:
    Impl *impl;
  };

  class IdleTestClient {
  public:
    explicit IdleTestClient(const std::string &socket);
    ~IdleTestClient();
    IdleTestClient(const IdleTestClient &) = delete;
    IdleTestClient &operator=(const IdleTestClient &) = delete;

    bool ok() const;
    void flush();
    void pump();

    // -- grown in Task 10 --
    void createNotification(uint32_t timeout_ms);  // on the bound wl_seat
    int idledCount() const;
    int resumedCount() const;

    struct Impl;

  private:
    Impl *impl;
  };

} // namespace bbai::test

#endif // BLACKBOXAI_LOCK_TEST_CLIENT_HH
