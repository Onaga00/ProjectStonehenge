#include "platform/crash.h"

#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "platform/log.h"

namespace ac04::platform {
namespace {

CrashContextProvider g_provider = nullptr;

// Formats "module+0x1234" for an address, without allocating.
void DescribeAddress(const void* address, char* out, size_t size) {
    Dl_info info{};
    if (::dladdr(address, &info) && info.dli_fbase) {
        const char* base = info.dli_fname ? info.dli_fname : "?";
        for (const char* p = base; *p; ++p)
            if (*p == '/') base = p + 1;
        std::snprintf(out, size, "%s+0x%llx", base,
                      static_cast<unsigned long long>(reinterpret_cast<const char*>(address) -
                                                      static_cast<const char*>(info.dli_fbase)));
    } else {
        std::snprintf(out, size, "%p", address);
    }
}

constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};

void CrashSignal(int sig, siginfo_t* info, void*) {
    char text[1024];
    std::snprintf(text, sizeof(text), "\n*** CRASH: signal %d (%s) at address %p\n", sig,
                  sig == SIGSEGV   ? "SIGSEGV"
                  : sig == SIGBUS  ? "SIGBUS"
                  : sig == SIGFPE  ? "SIGFPE"
                  : sig == SIGILL  ? "SIGILL"
                  : sig == SIGABRT ? "SIGABRT"
                                   : "?",
                  info ? info->si_addr : nullptr);
    log::WriteRawUnsafe(text);

    // Return addresses of the crashing thread, from the handler (as on Windows).
    void* frames[32];
    const int count = ::backtrace(frames, 32);
    char where[512];
    for (int i = 0; i < count; ++i) {
        DescribeAddress(frames[i], where, sizeof(where));
        std::snprintf(text, sizeof(text), "    #%02d %s\n", i, where);
        log::WriteRawUnsafe(text);
    }
    if (g_provider) {
        char extra[512] = {};
        g_provider(extra, sizeof(extra) - 1);
        std::snprintf(text, sizeof(text), "    context: %s\n", extra);
        log::WriteRawUnsafe(text);
    }
    log::WriteRawUnsafe("*** end of crash report\n");

    // Default action (core dump / termination) with the original signal.
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

[[noreturn]] void TerminateHandler() {
    log::WriteRawUnsafe("\n*** CRASH: std::terminate called (unhandled C++ exception)\n");
    std::abort();
}

}  // namespace

void InstallCrashHandler() {
    // backtrace() loads libgcc on first use, which is not safe inside a signal handler: do it now.
    void* warmup[1];
    ::backtrace(warmup, 1);

    // Own stack for the handler, so a stack overflow can still be reported.
    static char alt_stack[64 * 1024];
    stack_t ss{};
    ss.ss_sp = alt_stack;
    ss.ss_size = sizeof(alt_stack);
    ::sigaltstack(&ss, nullptr);

    struct sigaction sa {};
    sa.sa_sigaction = CrashSignal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    for (int sig : kSignals) ::sigaction(sig, &sa, nullptr);
    std::set_terminate(TerminateHandler);
}

void SetCrashContextProvider(CrashContextProvider provider) {
    g_provider = provider;
}

void DescribeCallers(char* buffer, unsigned size, unsigned skip, unsigned count) {
    if (size == 0) return;
    buffer[0] = 0;
    void* frames[64];
    if (count > 32) count = 32;
    if (skip > 31) skip = 31;  // count + skip + 1 <= 64 frames
    const int total = ::backtrace(frames, static_cast<int>(count + skip + 1));
    unsigned used = 0;
    for (int i = static_cast<int>(skip) + 1; i < total && used + 1 < size; ++i) {
        char where[512];
        DescribeAddress(frames[i], where, sizeof(where));
        const int w = std::snprintf(buffer + used, size - used, "%s%s",
                                    i > static_cast<int>(skip) + 1 ? " " : "", where);
        if (w < 0) break;
        used += static_cast<unsigned>(w);
    }
}

}  // namespace ac04::platform
