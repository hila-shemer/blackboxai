// Fatal-signal breadcrumb (lifetime-hardening, Phase 4). One async-signal-safe
// line on stderr - journald keeps it next to the session logs - then the
// default action runs so the core dump still drops. The breadcrumb is what
// turns "the session died" into "SIGSEGV at 0x... , go run coredumpctl".
#ifndef BLACKBOXAI_CRASH_HH
#define BLACKBOXAI_CRASH_HH

#include <cstddef>

namespace bbai::crash {

  // Render the one-line breadcrumb into buf. Async-signal-safe by construction:
  // no allocation, no stdio, no locale. Truncates at cap. Returns bytes written.
  size_t formatBreadcrumb(char *buf, size_t cap, int sig, const void *addr);

  // Install for the fatal set (SEGV/ABRT/BUS/FPE/ILL). SA_RESETHAND: the
  // handler runs once, writes the breadcrumb, re-raises into the default
  // action - no handler loops, no swallowed cores.
  void installHandlers();

} // namespace bbai::crash

#endif // BLACKBOXAI_CRASH_HH
