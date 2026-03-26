#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <sys/resource.h>
#include <sys/time.h>

#include "blackbox.hpp"
#include "pool.hpp"

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

struct MmapAlloc {
    Node *alloc_list(size_t n) {
        pool_ = Pool<>::with_max_size(n * sizeof(Node));
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
    Pool<> pool_;
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
