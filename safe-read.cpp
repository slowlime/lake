#include "blackbox.hpp"
#include <atomic>
#include <cassert>
#include <csetjmp>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <unistd.h>

static struct sigaction prev_segv, prev_bus;
static sigjmp_buf jbuf;

static void dispatch_to_prev(int sig, siginfo_t *info, void *uctx) noexcept {
    const auto *prev = sig == SIGSEGV ? &prev_segv : &prev_bus;

    if ((prev->sa_flags & SA_SIGINFO) != 0) {
        prev->sa_sigaction(sig, info, uctx);
    } else if (prev->sa_handler == nullptr || prev->sa_handler == SIG_IGN ||
               prev->sa_handler == SIG_DFL) {
        // re-raise the signal.
        struct sigaction act{};
        act.sa_handler = SIG_DFL;
        sigaction(sig, &act, nullptr);
        raise(sig);
    } else {
        prev->sa_handler(sig);
    }
}

static void on_access_fault(int sig, siginfo_t *info, void *uctx) noexcept {
    if (info->si_code == SI_USER) {
        return;
    }

    siglongjmp(jbuf, 1);
}

static std::optional<uint8_t> safe_read_u8(const void *ptr) noexcept {
    struct sigaction act{};
    sigemptyset(&act.sa_mask);
    sigaddset(&act.sa_mask, SIGBUS);
    sigaddset(&act.sa_mask, SIGSEGV);
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = on_access_fault;

    if (sigaction(SIGSEGV, &act, &prev_segv) < 0) {
        perror("could not install a SIGSEGV handler");
        exit(1);
    }

    if (sigaction(SIGBUS, &act, &prev_bus) < 0) {
        perror("could not install a SIGBUS handler");
        exit(1);
    }

    uint8_t result = 0;
    bool faulted = false;

    if (sigsetjmp(jbuf, 1) == 0) {
        result = uint8_t(*reinterpret_cast<const char *>(ptr));
    } else {
        faulted = true;
    }

    sigaction(SIGSEGV, &prev_segv, nullptr);
    sigaction(SIGBUS, &prev_segv, nullptr);

    if (faulted) {
        return {};
    }

    return result;
}

int main(int, char **) {
    uint8_t foo = 42;
    const void *ptr = nullptr;
    blackbox(static_cast<void *>(&ptr));

    assert(!safe_read_u8(ptr).has_value());
    assert(safe_read_u8(&foo) == 42);
}
