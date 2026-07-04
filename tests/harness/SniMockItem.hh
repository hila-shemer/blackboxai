// Forked mock SNI publisher: a real second process with its own bus connection
// (like an actual tray app), remote-controlled over a command pipe, reporting
// what happens to it over a report pipe. Deliberately OPAQUE - no sd-bus types
// in this header (same stance as TestClient.hh).
#ifndef BLACKBOXAI_SNI_MOCK_ITEM_HH
#define BLACKBOXAI_SNI_MOCK_ITEM_HH

#include <functional>
#include <string>
#include <sys/types.h>

namespace bbai::test {

  // The two known 2x2 ARGB32 network-order icons the mock serves (16 bytes,
  // A,R,G,B per pixel). Icon2 is what updateIcon() switches to.
  extern const unsigned char kSniMockIcon[16];
  extern const unsigned char kSniMockIcon2[16];

  class SniMockChild {
  public:
    // fork()s immediately. The child connects to the session bus, publishes an
    // org.kde.StatusNotifierItem object at /StatusNotifierItem, registers with
    // the watcher (retrying until one exists), then reports "registered".
    // register_by_name=true: the child claims org.test.SniMock and registers
    // by that name (KDE convention) instead of by object path (ayatana).
    explicit SniMockChild(bool register_by_name = false);
    ~SniMockChild();                          // SIGKILL + reap if still alive
    SniMockChild(const SniMockChild &) = delete;
    SniMockChild &operator=(const SniMockChild &) = delete;

    bool ok() const { return pid_ > 0; }
    pid_t pid() const { return pid_; }

    void updateIcon();     // serve kSniMockIcon2 + emit NewIcon
    void updateStatus();   // Status -> "NeedsAttention" + emit NewStatus
    void quit();           // clean exit (bus connection closed politely)
    void killHard();       // SIGKILL - no D-Bus goodbye, only the name drop

    // Next line from the report pipe ("registered", "Activate 10 20", ...);
    // "" on timeout. `pump` runs every poll slice - pass the Host's pump so
    // the watcher in THIS process keeps serving while we wait.
    std::string waitReport(int timeout_ms = 5000, std::function<void()> pump = {});

  private:
    void send(char c);
    pid_t pid_ = -1;
    int cmd_fd_ = -1;      // parent -> child commands
    int report_fd_ = -1;   // child -> parent reports
    std::string buf_;      // partial-line carry-over
  };

} // namespace bbai::test

#endif // BLACKBOXAI_SNI_MOCK_ITEM_HH
