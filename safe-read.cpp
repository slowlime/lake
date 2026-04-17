#include "blackbox.hpp"
#include <cassert>
#include <csetjmp>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <optional>
#include <sys/mman.h>
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
        dispatch_to_prev(sig, info, uctx);
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

    void *reg = mmap(nullptr, 64, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);

    if (reg == MAP_FAILED) {
        perror("could not mmap memory");
        return 1;
    }

    assert(!safe_read_u8(reg).has_value());

    if (munmap(reg, 64) < 0) {
        perror("could not munmap memory");
        return 1;
    }

    assert(!safe_read_u8(reg).has_value());

    char tmp_name[] = "/tmp/safe-read-XXXXXX";
    auto fd = mkstemp(tmp_name);

    if (fd < 0) {
        perror("could not create a temporary file");
        return 1;
    }

    void *file_reg = mmap(nullptr, 64, PROT_READ, MAP_PRIVATE, fd, 0);

    if (unlink(tmp_name) < 0) {
        perror("could not remove the temporary file");
    }

    if (file_reg == MAP_FAILED) {
        perror("could not mmap /dev/null");
        return 1;
    }

    assert(!safe_read_u8(file_reg).has_value());

    if (munmap(file_reg, 64) < 0) {
        perror("could not munmap /dev/null");
    }

    std::cout << "all assertions passed\n";
}
