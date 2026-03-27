#include "pool.hpp"

#include <cstdlib>
#include <cstring>
#include <unistd.h>

size_t page_size = ([] {
    auto result = sysconf(_SC_PAGE_SIZE);

    if (result < 0) {
        perror("Cannot determine the page size");
        exit(EXIT_FAILURE);
    }

    // for peace of mind.
    assert((result & result - 1) == 0);

    return result;
})();

PoolRegistration *PoolRegistration::add(std::string_view name, void *start, void *end) {
    // this technically makes add() an O(n) operation.
    cleanup();

    auto *name_copy = new char[name.length() + 1];
    memcpy(name_copy, name.data(), name.length());
    name_copy[name.length()] = '\0';

    auto *result = new PoolRegistration(name_copy, start, end);
    auto *next = head.load(std::memory_order_relaxed);

    do {
        result->next_.store(next, std::memory_order_relaxed);
    } while (!head.compare_exchange_weak(
        next, result, std::memory_order_relaxed, std::memory_order_relaxed
    ));

    return result;
}

void PoolRegistration::remove() noexcept {
    removed_.store(true, std::memory_order_relaxed);
    // synchronizes with cleanup() to make sure `this` is removed if it starts cleanup.
    has_removed.store(true, std::memory_order_release);
}

PoolRegistration::PoolRegistration(char *name, void *start, void *end) noexcept
    : name_(name), start_(start), end_(end) {}

void PoolRegistration::cleanup() noexcept {
    // this method ensures the following invariant: if at any point the list contains an element
    // marked for removal while cleanup is not underway, has_removed must be true. however,
    // has_removed is allowed to be true even if there are no such elements in the list.

    bool expected = false;

    // ensure we're the only one cleaning up.
    if (!cleanup_in_progress.compare_exchange_strong(
            expected, true, std::memory_order_acquire, std::memory_order_relaxed
        )) {
        return;
    }

    expected = true;

    // check if we have anything to clean up.
    // (acquire synchronizes with remove()).
    if (!has_removed.compare_exchange_strong(
            expected, false, std::memory_order_acquire, std::memory_order_relaxed
        )) {
        // we don't! finish early.
        cleanup_in_progress.store(false, std::memory_order_release);

        return;
    }

    // we rely on the fact that the only concurrent changes happening to the list's structure are
    // addition of new nodes at the head. this means that ->next_ pointers stay the same unless we
    // change them here ourselves.
    auto *prev = &head;

    while (true) {
        auto *registration = prev->load(std::memory_order_relaxed);

        if (registration == nullptr) {
            break;
        }

        if (registration->removed_.load(std::memory_order_relaxed)) {
            auto *next = registration->next_.load(std::memory_order_relaxed);

            // yank the registration out of the list.
            if (!prev->compare_exchange_weak(registration, next, std::memory_order_relaxed)) {
                // no such luck; retry. (this means we are still at the head of the list.)
                continue;
            }

            delete[] registration->name_.load(std::memory_order_relaxed);
            delete registration;

            // we've changed *prev to point to next, so just continue the loop.
            continue;
        }

        // examine the next element.
        prev = &registration->next_;
    }

    cleanup_in_progress.store(false, std::memory_order_release);
}

std::atomic<PoolRegistration *> PoolRegistration::head = nullptr;
std::atomic<bool> PoolRegistration::has_removed = false;
std::atomic<bool> PoolRegistration::cleanup_in_progress = false;

namespace {

void (*prev_handler)(int) = nullptr;
void (*prev_sigaction)(int, siginfo_t *, void *) = nullptr;

} // namespace

void PoolRegistration::register_sigsegv_handler() noexcept {
    struct sigaction prev;

    if (sigaction(SIGSEGV, nullptr, &prev) < 0) {
        perror("Could not retrieve the previous SIGSEGV handler");
        return;
    }

    if ((prev.sa_flags & SA_SIGINFO) != 0) {
        prev_sigaction = prev.sa_sigaction;
    } else {
        prev_handler = prev.sa_handler;
    }

    struct sigaction act{};
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = on_sigsegv;

    if (sigaction(SIGSEGV, &act, nullptr) < 0) {
        perror("Could not install a SIGSEGV handler");
        return;
    }
}

namespace {

void reraise_signal(int sig) noexcept {
    struct sigaction act {};
    act.sa_handler = SIG_DFL;
    sigaction(sig, &act, nullptr);
    raise(sig);
}

} // namespace

void PoolRegistration::on_sigsegv(int sig, siginfo_t *info, void *uctx) noexcept {
    void *addr = info->si_addr;

    const PoolRegistration *registration;

    for (const auto *next = &head;
         (registration = next->load(std::memory_order_relaxed)) != nullptr;
         next = &registration->next_) {
        if (registration->removed_.load(std::memory_order_relaxed)) {
            continue;
        }

        if (addr < registration->start_.load(std::memory_order_relaxed) ||
            addr >= registration->end_.load(std::memory_order_relaxed)) {
            continue;
        }

        static const char overflow_msg_start[] = "overflow detected in pool '";
        static const char overflow_msg_end[] = "'\n";

        const auto *name = registration->name_.load(std::memory_order_relaxed);

        write(STDERR_FILENO, overflow_msg_start, sizeof(overflow_msg_start) - 1);
        write(STDERR_FILENO, name, strlen(name));
        write(STDERR_FILENO, overflow_msg_end, sizeof(overflow_msg_end) - 1);

        reraise_signal(sig);
    }

    // dispatch to the previous handler.
    if (prev_sigaction != nullptr) {
        prev_sigaction(sig, info, uctx);
    } else if (prev_handler == nullptr || prev_handler == SIG_IGN || prev_handler == SIG_DFL) {
        reraise_signal(sig);
    } else {
        prev_handler(sig);
    }
}
