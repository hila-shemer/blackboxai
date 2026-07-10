#include "Crash.hh"

#include <csignal>
#include <cstdint>
#include <initializer_list>
#include <unistd.h>

namespace bbai::crash {

  namespace {

    // Bounded byte-wise append - the whole formatter builds on these two so a
    // short buffer truncates instead of overflowing.
    size_t put(char *buf, size_t cap, size_t at, const char *s) {
      while (*s && at < cap) buf[at++] = *s++;
      return at;
    }

    size_t putUnsigned(char *buf, size_t cap, size_t at, uint64_t v) {
      char tmp[20];
      size_t n = 0;
      do { tmp[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
      while (n && at < cap) buf[at++] = tmp[--n];
      return at;
    }

    size_t putHex(char *buf, size_t cap, size_t at, uint64_t v) {
      static const char digits[] = "0123456789abcdef";
      char tmp[16];
      size_t n = 0;
      do { tmp[n++] = digits[v & 0xf]; v >>= 4; } while (v);
      at = put(buf, cap, at, "0x");
      while (n && at < cap) buf[at++] = tmp[--n];
      return at;
    }

    const char *signalName(int sig) {
      switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGBUS:  return "SIGBUS";
        case SIGFPE:  return "SIGFPE";
        case SIGILL:  return "SIGILL";
        default:      return nullptr;
      }
    }

    void handler(int sig, siginfo_t *info, void *) {
      char buf[160];
      // The fault address is meaningful for the memory signals only; SIGABRT's
      // si_addr is noise.
      const void *addr =
        (info && (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL))
          ? info->si_addr : nullptr;
      size_t n = formatBreadcrumb(buf, sizeof buf, sig, addr);
      ssize_t w = write(STDERR_FILENO, buf, n);
      (void)w;                 // a full pipe must not stop the re-raise
      raise(sig);              // SA_RESETHAND restored the default: core drops
    }

  } // namespace

  size_t formatBreadcrumb(char *buf, size_t cap, int sig, const void *addr) {
    size_t at = put(buf, cap, 0, "blackboxai: FATAL signal ");
    at = putUnsigned(buf, cap, at, static_cast<uint64_t>(sig));
    if (const char *name = signalName(sig)) {
      at = put(buf, cap, at, " (");
      at = put(buf, cap, at, name);
      at = put(buf, cap, at, ")");
    }
    if (addr) {
      at = put(buf, cap, at, " at ");
      at = putHex(buf, cap, at, reinterpret_cast<uint64_t>(addr));
    }
    at = put(buf, cap, at, " - dumping core, see coredumpctl\n");
    return at;
  }

  void installHandlers() {
    struct sigaction sa = {};
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    for (int sig : { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL })
      sigaction(sig, &sa, nullptr);
  }

} // namespace bbai::crash
