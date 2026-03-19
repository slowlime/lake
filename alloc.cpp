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

size_t round_up(size_t n, size_t unit) {
    auto mod = n % unit;

    return mod == 0 ? n : n + unit - mod;
}

struct MmapAlloc {
    static constexpr size_t guard_page_count = 8;

    Node *start = nullptr;
    size_t page_size;
    size_t alloc_size = 0;
    size_t guard_size;

    MmapAlloc() {
        auto page_size = sysconf(_SC_PAGE_SIZE);

        if (page_size < 0) {
            perror("Cannot determine the page size");
            exit(EXIT_FAILURE);
        }

        this->page_size = page_size;
        guard_size = this->page_size * guard_page_count;
    }

    Node *alloc_list(size_t n) {
        auto unguarded_size = round_up(n * sizeof(Node), page_size);
        alloc_size = unguarded_size + 2 * guard_size;

        void *alloc = mmap(
            nullptr,
            alloc_size,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_GROWSDOWN,
            0,
            0
        );

        if (alloc == MAP_FAILED) {
            perror("Cannot allocate memory");
            exit(EXIT_FAILURE);
        }

        auto *end = reinterpret_cast<Node *>(static_cast<char *>(alloc) + alloc_size - guard_size);

        guard_pages(alloc);
        guard_pages(end);

        start = static_cast<Node *>(alloc);
        Node *result = nullptr;

        for (size_t i = 0; i < n; ++i) {
            result = new (static_cast<void *>(--end)) Node{
                .next = result,
                .node_id = i,
            };
        }

        return result;
    }

    void dealloc_list(Node *) {
        munmap(start, alloc_size);
        start = nullptr;
        alloc_size = 0;
    }

private:
    void guard_pages(void *lo) const {
        if (mprotect(lo, guard_size, PROT_NONE) == -1) {
            perror("Cannot add guard pages");
        }
    }
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
