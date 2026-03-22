#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

#include "blackbox.hpp"

namespace {

void print_usage() {
    std::cerr << "Usage: alloc {malloc|mmap}\n";
}

void get_usage(struct rusage &usage) {
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        perror("Cannot retrieve resource usage");
        exit(EXIT_FAILURE);
    }
}

size_t round_up(size_t n, size_t unit) {
    auto mod = n % unit;

    return mod == 0 ? n : n + unit - mod;
}

class Pool {
public:
    Pool() noexcept = default;

    static Pool with_max_size(size_t max) {
        return Pool(max);
    }

    void *alloc(size_t size) {
        return (end -= size);
    }

    template<class T>
    T *alloc() {
        return reinterpret_cast<T *>(alloc(sizeof(T)));
    }

    void dealloc() {
        if (munmap(arena, mmap_size) == -1) {
            perror("Could not unmap memory");
        }

        arena = nullptr;
        mmap_size = 0;
    }

private:
    explicit Pool(size_t max) {
        auto guard_size = guard_page_count * page_size;
        auto mmap_size = round_up(max) + guard_size;

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

        this->arena = arena;
        this->mmap_size = mmap_size;
        end = static_cast<char *>(arena) + mmap_size;
    }

    static size_t page_size;
    static constexpr size_t guard_page_count = 1;

    static size_t round_up(size_t n) {
        n += page_size - 1;

        return n & ~(page_size - 1);
    }

    void *arena = nullptr;
    char *end = nullptr;
    size_t mmap_size = 0;
};

size_t Pool::page_size = ([] {
    auto result = sysconf(_SC_PAGE_SIZE);

    if (result < 0) {
        perror("Cannot determine the page size");
        exit(EXIT_FAILURE);
    }

    // for peace of mind.
    assert((result & result - 1) == 0);

    return result;
})();

struct Node {
    Node *next;
    size_t node_id;
};

struct NewDeleteAlloc {
    Node *alloc_list(size_t n) {
        Node *result = nullptr;

        for (size_t i = 0; i < n; ++i) {
            result = new Node{
                .next = result,
                .node_id = i,
            };
        }

        return result;
    }

    void dealloc_list(Node *list) {
        while (list != nullptr) {
            auto *node = list;
            list = list->next;
            delete node;
        }
    }
};

struct MmapAlloc {
    Node *alloc_list(size_t n) {
        pool_ = Pool::with_max_size(n * sizeof(Node));
        Node *result = nullptr;

        for (size_t i = 0; i < n; ++i) {
            result = new (pool_.alloc<Node>()) Node{
                .next = result,
                .node_id = i,
            };
        }

        return result;
    }

    void dealloc_list(Node *) {
        pool_.dealloc();
    }

private:
    Pool pool_;
};

template<class Alloc>
inline void test(Alloc alloc, unsigned n) {
    struct rusage start, finish;
    get_usage(start);
    auto *list = alloc.alloc_list(n);

    // to avoid the allocations being optimized out.
    blackbox(list);

    get_usage(finish);
    alloc.dealloc_list(list);

    struct timeval diff;
    timersub(&finish.ru_utime, &start.ru_utime, &diff);
    uint64_t time_used = diff.tv_sec * 1'000'000 + diff.tv_usec;
    std::cout << "Time used: " << time_used << " usec\n";

    uint64_t mem_used = (finish.ru_maxrss - start.ru_maxrss) * 1024;
    std::cout << "Memory used: " << mem_used << " bytes\n";

    auto mem_required = n * sizeof(Node);
    auto overhead = (double(mem_used) - double(mem_required)) * 100 / double(mem_used);
    std::cout << "Overhead: " << std::fixed << std::setw(4) << std::setprecision(1) << overhead
              << "%\n";
}

constexpr size_t n = 10'000'000;

} // namespace

int main(const int argc, const char *argv[]) {
    if (argc != 2) {
        print_usage();

        return EXIT_FAILURE;
    }

    std::string_view mode = argv[1];

    if (mode == "malloc") {
        test(NewDeleteAlloc{}, n);
    } else if (mode == "mmap") {
        test(MmapAlloc{}, n);
    } else {
        print_usage();

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
