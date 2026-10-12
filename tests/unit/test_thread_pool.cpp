#include "tcp/thread_pool.hpp"
#include <cassert>
#include <iostream>
#include <atomic>
#include <stdexcept>
#include <chrono>

using namespace netstream;

void test_basic_execution() {
    ThreadPool pool(4, 32);
    std::atomic<int> counter{0};

    for (int i = 0; i < 50; ++i) {
        bool ok = pool.submit([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
        assert(ok);
    }

    pool.shutdown(true);
    assert(counter.load() == 50);
    std::cout << "[PASS] test_basic_execution\n";
}

void test_bounded_queue_rejection() {
    ThreadPool pool(1, 2); // 1 worker, capacity 2
    std::atomic<bool> block_worker{true};
    std::atomic<bool> worker_running{false};

    // First task blocks the worker
    pool.submit([&]() {
        worker_running = true;
        while (block_worker.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    while (!worker_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // Now fill queue of capacity 2
    bool q1 = pool.submit([]() {}, FullQueuePolicy::Reject);
    bool q2 = pool.submit([]() {}, FullQueuePolicy::Reject);
    assert(q1);
    assert(q2);

    // Third task must be rejected!
    bool q3 = pool.submit([]() {}, FullQueuePolicy::Reject);
    assert(!q3);

    block_worker.store(false);
    pool.shutdown(true);
    std::cout << "[PASS] test_bounded_queue_rejection\n";
}

void test_exception_safety() {
    ThreadPool pool(2, 16);
    std::atomic<int> completed{0};

    // Task that throws
    pool.submit([]() {
        throw std::runtime_error("Worker exception simulation");
    });

    // Subsequent task should still execute normally
    pool.submit([&completed]() {
        completed.fetch_add(1);
    });

    pool.shutdown(true);
    assert(completed.load() == 1);
    std::cout << "[PASS] test_exception_safety\n";
}

void test_submission_during_shutdown() {
    ThreadPool pool(2, 16);
    pool.shutdown(true);

    bool ok = pool.submit([]() {});
    assert(!ok);
    std::cout << "[PASS] test_submission_during_shutdown\n";
}

int main() {
    std::cout << "--- Running ThreadPool Unit Tests ---\n";
    test_basic_execution();
    test_bounded_queue_rejection();
    test_exception_safety();
    test_submission_during_shutdown();
    std::cout << "All ThreadPool unit tests passed successfully!\n";
    return 0;
}
