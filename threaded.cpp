#include <barrier>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string_view>
#include <sys/resource.h>
#include <sys/time.h>
#include <thread>
#include <utility>
#include <vector>

#include "blackbox.hpp"
#include "pool.hpp"

namespace {

void print_usage() {
    std::cerr << "Usage: threaded {malloc|mutex|atomic|thread-local}\n";
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

template<class Self>
struct PerElementStrategy {
    void dealloc_list(size_t thread_id, Node *list) {
        while (list != nullptr) {
            auto *node = list;
            list = list->next;
            static_cast<Self *>(this)->dealloc_node(thread_id, node);
        }
    }

private:
    PerElementStrategy() = default;
    friend Self;
};

struct NewDeleteStrategy : PerElementStrategy<NewDeleteStrategy> {
    size_t n;

    NewDeleteStrategy(size_t threads, size_t n) : n(n) {}

    void init(size_t thread_id) {}

    Node *alloc_node(size_t thread_id, size_t node_id, Node *next) const {
        return new Node{
            .next = next,
            .node_id = node_id,
        };
    }

    void dealloc_node(size_t thread_id, Node *node) const {
        delete node;
    }
};

struct MutexStrategy {
    Pool<UnsyncBump> pool;
    std::mutex mtx;

    MutexStrategy(size_t threads, size_t n) {
        pool = Pool<>::with_max_size("global pool", n * threads * sizeof(Node));
    }

    void init(size_t thread_id) {}

    Node *alloc_node(size_t thread_id, size_t node_id, Node *next) {
        Node *result;

        {
            std::lock_guard<std::mutex> guard(mtx);
            result = pool.alloc<Node>();
        }

        return new (result) Node{
            .next = next,
            .node_id = node_id,
        };
    }

    void dealloc_list(size_t thread_id, Node *) {
        if (thread_id == 0) {
            pool.dealloc();
        }
    }
};

struct AtomicStrategy {
    Pool<AtomicBump> pool;

    AtomicStrategy(size_t threads, size_t n) {
        pool = Pool<AtomicBump>::with_max_size("global pool", n * threads * sizeof(Node));
    }

    void init(size_t thread_id) {}

    Node *alloc_node(size_t thread_id, size_t node_id, Node *next) {
        return new (pool.alloc<Node>()) Node{
            .next = next,
            .node_id = node_id,
        };
    }

    void dealloc_list(size_t thread_id, Node *) {
        if (thread_id == 0) {
            pool.dealloc();
        }
    }
};

struct ThreadLocalStrategy {
    static thread_local Pool<UnsyncBump> pool;
    size_t n;

    ThreadLocalStrategy(size_t threads, size_t n) : n(n) {}

    void init(size_t thread_id) const {
        std::string name;

        {
            std::ostringstream ss;
            ss << "thread-local pool #" << thread_id;
            name = std::move(ss).str();
        }

        pool = Pool<UnsyncBump>::with_max_size(name, n * sizeof(Node));
    }

    Node *alloc_node(size_t thread_id, size_t node_id, Node *next) {
        return new (pool.alloc<Node>()) Node{
            .next = next,
            .node_id = node_id,
        };
    }

    void dealloc_list(size_t thread_id, Node *) {
        pool.dealloc();
    }
};

thread_local Pool<UnsyncBump> ThreadLocalStrategy::pool;

template<class Strategy>
void run_worker(Strategy &strategy, std::barrier<> &barrier, size_t thread_id, size_t n) {
    // phase 1: thread creation.
    strategy.init(thread_id);
    barrier.arrive_and_wait();

    // phase 2: allocation.
    Node *list = nullptr;

    for (size_t i = 0; i < n; ++i) {
        list = strategy.alloc_node(thread_id, i, list);
    }

    // so that the compile doesn't optimize allocations out.
    blackbox(list);

    barrier.arrive_and_wait();

    // phase 3: measurement.
    barrier.arrive_and_wait();

    // phase 4: deallocation.
    strategy.dealloc_list(thread_id, list);
}

template<class Strategy>
void test(size_t n, size_t thread_count) {
    Strategy strategy(thread_count, n);
    std::barrier barrier(std::ptrdiff_t(thread_count + 1));

    std::cout << "Running with " << thread_count << " threads\n";

    // phase 1: thread creation.
    std::vector<std::thread> workers;
    workers.reserve(thread_count);

    for (size_t thread_id = 0; thread_id < thread_count; ++thread_id) {
        workers.emplace_back(
            run_worker<Strategy>,
            std::ref(strategy),
            std::ref(barrier),
            thread_id,
            n
        );
    }

    struct rusage start, finish;
    get_usage(start);
    barrier.arrive_and_wait();

    // phase 2: allocation.
    barrier.arrive_and_wait();

    // phase 3: measurement.
    get_usage(finish);
    barrier.arrive_and_wait();

    // phase 4: deallocation.
    for (auto &worker : workers) {
        worker.join();
    }

    struct timeval diff;
    timersub(&finish.ru_utime, &start.ru_utime, &diff);
    uint64_t time_used = diff.tv_sec * 1'000'000 + diff.tv_usec;
    std::cout << "Time used: " << time_used << " usec\n";

    uint64_t mem_used = (finish.ru_maxrss - start.ru_maxrss) * 1024;
    std::cout << "Memory used: " << mem_used << " bytes\n";

    auto mem_required = thread_count * n * sizeof(Node);
    auto overhead = (double(mem_used) - double(mem_required)) * 100 / double(mem_used);
    std::cout << "Overhead: " << std::fixed << std::setw(4) << std::setprecision(1) << overhead
              << "%\n";
}

constexpr size_t n = 10'000'000;
constexpr size_t thread_count = 16;

} // namespace

int main(const int argc, const char *argv[]) {
    PoolRegistration::register_sigsegv_handler();

    if (argc != 2) {
        print_usage();

        return EXIT_FAILURE;
    }

    std::string_view mode = argv[1];

    if (mode == "malloc") {
        test<NewDeleteStrategy>(n, thread_count);
    } else if (mode == "mutex") {
        test<MutexStrategy>(n, thread_count);
    } else if (mode == "atomic") {
        test<AtomicStrategy>(n, thread_count);
    } else if (mode == "thread-local") {
        test<ThreadLocalStrategy>(n, thread_count);
    } else {
        print_usage();

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
