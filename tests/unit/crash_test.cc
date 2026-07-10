// Crash forensics (lifetime-hardening spec, Phase 4): the fatal-signal
// breadcrumb. The formatter is pure and async-signal-safe by construction; the
// handler test forks a child that installs the handlers and segfaults, then
// asserts the breadcrumb landed on the child's stderr AND the child still died
// by the signal's default action (so the core dump is not swallowed).
#include <doctest/doctest.h>
#include "Crash.hh"

#include <csignal>
#include <cstring>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

TEST_CASE("breadcrumb formatting: signal name and fault address, no libc formatting") {
    char buf[160];
    size_t n = bbai::crash::formatBreadcrumb(buf, sizeof buf, SIGSEGV,
                                             reinterpret_cast<void *>(0x7f00dead0000u));
    std::string s(buf, n);
    CHECK(s == "blackboxai: FATAL signal 11 (SIGSEGV) at 0x7f00dead0000 - "
               "dumping core, see coredumpctl\n");

    n = bbai::crash::formatBreadcrumb(buf, sizeof buf, SIGABRT, nullptr);
    s.assign(buf, n);
    CHECK(s == "blackboxai: FATAL signal 6 (SIGABRT) - dumping core, see coredumpctl\n");

    // Unknown signal number still degrades to something greppable.
    n = bbai::crash::formatBreadcrumb(buf, sizeof buf, 64, nullptr);
    s.assign(buf, n);
    CHECK(s == "blackboxai: FATAL signal 64 - dumping core, see coredumpctl\n");
}

TEST_CASE("breadcrumb truncates instead of overflowing a tiny buffer") {
    char buf[16];
    size_t n = bbai::crash::formatBreadcrumb(buf, sizeof buf, SIGSEGV,
                                             reinterpret_cast<void *>(0x1234u));
    CHECK(n <= sizeof buf);
    CHECK(std::string(buf, n).substr(0, 10) == "blackboxai");
}

// Under ASAN the sanitizer owns SIGSEGV (its interceptor produces the UAF
// reports this suite exists for) and the child's death mode changes; the
// plain-build run of this same case covers the production handler.
#ifndef __SANITIZE_ADDRESS__
TEST_CASE("a crashing child emits the breadcrumb and still dies by the signal") {
    int fds[2];
    REQUIRE(pipe(fds) == 0);

    pid_t pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        // Child: breadcrumbs to the pipe, no core file litter from the test.
        dup2(fds[1], STDERR_FILENO);
        close(fds[0]);
        close(fds[1]);
        struct rlimit no_core = {0, 0};
        setrlimit(RLIMIT_CORE, &no_core);
        bbai::crash::installHandlers();
        volatile int *p = nullptr;
        *p = 42;                       // SIGSEGV
        _exit(0);                      // not reached
    }
    close(fds[1]);
    char buf[256] = {};
    ssize_t got = 0, r;
    while ((r = read(fds[0], buf + got, sizeof(buf) - 1 - got)) > 0) got += r;
    close(fds[0]);

    int status = 0;
    REQUIRE(waitpid(pid, &status, 0) == pid);
    CHECK(WIFSIGNALED(status));        // default action ran - core not swallowed
    CHECK(WTERMSIG(status) == SIGSEGV);
    CHECK(std::string(buf).find("blackboxai: FATAL signal 11 (SIGSEGV)")
          != std::string::npos);
}
#endif // __SANITIZE_ADDRESS__
