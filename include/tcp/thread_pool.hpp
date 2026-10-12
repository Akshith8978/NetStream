#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <cstddef>
#include <chrono>

namespace netstream {

enum class FullQueuePolicy {
    Block,
    Reject
};

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads, size_t max_queue_capacity = 1024);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * @brief Submit a callable task to the thread pool.
     * @param task Callable taking no arguments.
     * @param policy FullQueuePolicy::Block waits for queue space; Reject returns false immediately if full.
     * @return true if accepted, false if rejected due to full queue or pool shutdown.
     */
    bool submit(std::function<void()> task, FullQueuePolicy policy = FullQueuePolicy::Block);

    /**
     * @brief Shut down the thread pool and join all worker threads.
     * @param drain If true, executes remaining tasks before workers exit. If false, discards unstarted tasks.
     */
    void shutdown(bool drain = true);

    [[nodiscard]] bool is_stopped() const noexcept;
    [[nodiscard]] size_t queue_size() const;
    [[nodiscard]] size_t queue_capacity() const noexcept;
    [[nodiscard]] size_t num_threads() const noexcept;

private:
    void worker_loop();

    size_t num_threads_;
    size_t max_queue_capacity_;
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> task_queue_;

    mutable std::mutex queue_mutex_;
    std::condition_variable cv_not_empty_;
    std::condition_variable cv_not_full_;
    std::atomic<bool> stop_{false};
    bool drain_on_stop_{true};
};

} // namespace netstream
