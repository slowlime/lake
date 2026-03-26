#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>

extern size_t page_size;

inline size_t round_up(size_t n, size_t alignment) {
    n += alignment - 1;

    return n & ~(alignment - 1);
}

inline char *bump_aligned(char *end, size_t size, size_t align) {
    if ((uintptr_t(end) & align - 1) != 0) [[unlikely]] {
        // round down to the nearest alignment boundary.
        end -= uintptr_t(end) & align - 1;
    }

    return end - size;
}

struct UnsyncBump {
    UnsyncBump(char *end) noexcept : end(end) {}

    void *bump(size_t size, size_t align) noexcept {
        return end = bump_aligned(end, size, align);
    }

    char *end;
};

struct AtomicBump {
    static_assert(std::atomic<char *>::is_always_lock_free);

    AtomicBump(char *end) noexcept : end(end) {}

    // assumes `this` and `other` are not accessed concurrently.
    AtomicBump(AtomicBump &&other) noexcept {
        *this = std::move(other);
    }

    // assumes `this` and `other` are not accessed concurrently.
    AtomicBump &operator=(AtomicBump &&other) noexcept {
        if (&other == this) {
            return *this;
        }

        auto *other_end =
            other.end.exchange(end.load(std::memory_order_relaxed), std::memory_order_relaxed);
        end.store(other_end, std::memory_order_relaxed);

        return *this;
    }

    void *bump(size_t size, size_t align) noexcept {
        auto *prev = end.load(std::memory_order_relaxed);

        while (true) {
            auto *result = bump_aligned(prev, size, align);
            auto changed = end.compare_exchange_weak(
                prev, result, std::memory_order_relaxed, std::memory_order_relaxed
            );

            if (changed) {
                return result;
            }
        }
    }

    std::atomic<char *> end;
};

template<class Bump = UnsyncBump>
class Pool {
public:
    Pool() noexcept = default;

    Pool(Pool &&other) noexcept {
        *this = std::move(other);
    }

    Pool &operator=(Pool &&other) noexcept {
        if (&other == this) {
            return *this;
        }

        std::swap(arena_, other.arena_);
        std::swap(bump_, other.bump_);
        std::swap(mmap_size_, other.mmap_size_);

        return *this;
    }

    static Pool with_max_size(size_t max) {
        return Pool(max);
    }

    void *alloc(size_t size, size_t align) {
        return bump_.bump(size, align);
    }

    template<class T>
    T *alloc() {
        return reinterpret_cast<T *>(alloc(sizeof(T), alignof(T)));
    }

    void dealloc() {
        if (munmap(arena_, mmap_size_) == -1) {
            perror("Could not unmap memory");
        }

        arena_ = nullptr;
        mmap_size_ = 0;
    }

private:
    explicit Pool(size_t max) {
        auto guard_size = guard_page_count * page_size;
        auto mmap_size = round_up(max, page_size) + guard_size;

        void *arena = mmap(
            nullptr,
            mmap_size,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_GROWSDOWN,
            0,
            0
        );

        if (arena == MAP_FAILED) {
            perror("Cannot allocate memory");
            exit(EXIT_FAILURE);
        }

        if (mprotect(arena, guard_size, PROT_NONE) == -1) {
            perror("Cannot guard pages");
            exit(EXIT_FAILURE);
        }

        arena_ = arena;
        mmap_size_ = mmap_size;
        bump_ = static_cast<char *>(arena) + mmap_size;
    }

    static constexpr size_t guard_page_count = 1;

    void *arena_ = nullptr;
    Bump bump_ = nullptr;
    size_t mmap_size_ = 0;
};
