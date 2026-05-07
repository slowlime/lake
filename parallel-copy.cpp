#include <cassert>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <random>
#include <system_error>
#include <thread>
#include <vector>

#include "blackbox.hpp"

namespace {

class MemCopyThreadPool {
public:
    static MemCopyThreadPool instance;

    ~MemCopyThreadPool() noexcept {
        if (!init_ || threads_.empty()) {
            return;
        }

        {
            std::unique_lock lock(mtx_);
            shutting_down_ = true;
            has_work_.notify_all();
        }

        for (auto &thread : threads_) {
            thread.join();
        }
    }

    void init(size_t thread_count = std::thread::hardware_concurrency() - 1) {
        if (init_) {
            return;
        }

        for (size_t i = 0; i < thread_count; ++i) {
            threads_.emplace_back(&MemCopyThreadPool::worker, this, i);
        }

        init_ = true;
    }

    size_t thread_count() const noexcept {
        return threads_.size();
    }

private:
    friend void *parallel_memcpy(void *dst, const void *src, size_t size);
    friend struct JobResetter;

    struct Job {
        void *dst;
        const void *src;
        size_t size;

        Job split(size_t idx, size_t slices) const {
            auto chunk_size = size / slices;
            auto offset = chunk_size * idx;

            if (idx + 1 == slices) {
                chunk_size += size % slices;
            }

            return {
                .dst = static_cast<char *>(dst) + offset,
                .src = static_cast<const char *>(src) + offset,
                .size = chunk_size,
            };
        }
    };

    MemCopyThreadPool() = default;

    void worker(size_t thread_idx) {
        std::unique_lock lock(mtx_);

        while (true) {
            has_work_.wait(lock, [this] { return unprocessed_ > 0 || shutting_down_; });

            if (shutting_down_) {
                return;
            }

            auto worker_job = job_.split(thread_idx, job_slices());
            lock.unlock();

            memcpy(worker_job.dst, worker_job.src, worker_job.size);

            lock.lock();

            if (--unprocessed_ == 0) {
                finished_.notify_all();
            } else {
                finished_.wait(lock, [this] { return unprocessed_ == 0; });
            }
        }
    }

    size_t job_slices() const noexcept {
        return thread_count() + 1;
    }

    bool init_ = false;
    std::vector<std::thread> threads_;
    bool shutting_down_ = false;

    std::mutex mtx_;
    Job job_;
    std::condition_variable has_work_;
    std::condition_variable finished_;
    size_t unprocessed_ = 0;
};

MemCopyThreadPool MemCopyThreadPool::instance;

void *parallel_memcpy(void *dst, const void *src, size_t size) {
    auto &pool = MemCopyThreadPool::instance;
    pool.init();

    MemCopyThreadPool::Job job = {
        .dst = dst,
        .src = src,
        .size = size,
    };

    if (!pool.threads_.empty()) {
        std::unique_lock lock(pool.mtx_);
        pool.job_ = job;
        pool.unprocessed_ = pool.thread_count();
        pool.has_work_.notify_all();
        lock.unlock();

        job = job.split(pool.thread_count(), pool.job_slices());
    }

    memcpy(job.dst, job.src, job.size);

    if (!pool.threads_.empty()) {
        std::unique_lock lock(pool.mtx_);
        pool.finished_.wait(lock, [&pool] { return pool.unprocessed_ == 0; });
    }

    return dst;
}

void print_usage() {
    std::cerr << "Usage: parallel-copy [<thread count>]\n";
}

void generate_random(char *dst, size_t size) noexcept {
    std::ranlux24 rng;
    std::uniform_int_distribution<uint64_t> dist;

    size_t offset = reinterpret_cast<uintptr_t>(dst) & sizeof(uint64_t) - 1;

    auto v = dist(rng);
    memcpy(dst, &v, offset);

    size_t i;

    // write 64 bits per iteration so that it goes faster.
    for (i = offset; i < size; i += sizeof(uint64_t)) {
        auto v = dist(rng);
        memcpy(dst + i, &v, sizeof(uint64_t));
    }

    v = dist(rng);
    memcpy(dst + i, &v, size - i);
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        MemCopyThreadPool::instance.init();
    } else if (argc > 2) {
        print_usage();

        return 1;
    } else {
        size_t thread_count = 0;
        auto *arg_end = argv[1] + strlen(argv[1]);
        auto [ptr, ec] = std::from_chars(argv[1], arg_end, thread_count);

        if (ec != std::errc() || ptr != arg_end) {
            std::cerr << "Invalid thread count: " << argv[1] << "\n";
            print_usage();

            return 1;
        }

        MemCopyThreadPool::instance.init(thread_count);
    }

    constexpr size_t size = size_t(256) * 1024 * 1024;

    std::cerr << "Generating the workload\n";

    auto *dst = new char[size]{};
    auto *src = new char[size];

    generate_random(src, size);

    std::cerr << "Worker thread count: " << MemCopyThreadPool::instance.thread_count() << "\n";

    auto start = std::chrono::steady_clock::now();
    parallel_memcpy(dst, src, size);
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> delta = end - start;

    blackbox(dst);

    std::cerr << "Copied in " << delta << "\n";
    assert(memcmp(src, dst, size) == 0);

    return 0;
}
