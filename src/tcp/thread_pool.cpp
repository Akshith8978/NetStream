#include "tcp/thread_pool.hpp"
#include "common/logger.hpp"
#include <exception>

namespace netstream {

ThreadPool::ThreadPool(size_t num_threads, size_t max_queue_capacity)
    : num_threads_(num_threads > 0 ? num_threads : 1),
      max_queue_capacity_(max_queue_capacity > 0 ? max_queue_capacity : 1) {
    workers_.reserve(num_threads_);
    for (size_t i = 0; i < num_threads_; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

ThreadPool::~ThreadPool() {
    shutdown(true);
}

bool ThreadPool::submit(std::function<void()> task, FullQueuePolicy policy) {
    if (!task) return false;

    std::unique_lock<std::mutex> lock(queue_mutex_);
    if (stop_.load(std::memory_order_acquire)) {
        return false;
    }

    if (task_queue_.size() >= max_queue_capacity_) {
        if (policy == FullQueuePolicy::Reject) {
            return false;
        }
        // Block until space available or stopped
        cv_not_full_.wait(lock, [this]() {
            return stop_.load(std::memory_order_acquire) || (task_queue_.size() < max_queue_capacity_);
        });

        if (stop_.load(std::memory_order_acquire)) {
            return false;
        }
    }

    task_queue_.push(std::move(task));
    cv_not_empty_.notify_one();
    return true;
}

void ThreadPool::shutdown(bool drain) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (stop_.load(std::memory_order_relaxed)) {
            return;
        }
        drain_on_stop_ = drain;
        stop_.store(true, std::memory_order_release);
    }

    cv_not_empty_.notify_all();
    cv_not_full_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

bool ThreadPool::is_stopped() const noexcept {
    return stop_.load(std::memory_order_acquire);
}

size_t ThreadPool::queue_size() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return task_queue_.size();
}

size_t ThreadPool::queue_capacity() const noexcept {
    return max_queue_capacity_;
}

size_t ThreadPool::num_threads() const noexcept {
    return num_threads_;
}

void ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_not_empty_.wait(lock, [this]() {
                if (stop_.load(std::memory_order_acquire)) {
                    if (!drain_on_stop_ || task_queue_.empty()) {
                        return true;
                    }
                }
                return !task_queue_.empty();
            });

            if (stop_.load(std::memory_order_acquire)) {
                if (!drain_on_stop_ || task_queue_.empty()) {
                    return;
                }
            }

            task = std::move(task_queue_.front());
            task_queue_.pop();
            cv_not_full_.notify_one();
        }

        try {
            task();
        } catch (const std::exception& e) {
            LOG_ERROR << "ThreadPool task threw std::exception: " << e.what();
        } catch (...) {
            LOG_ERROR << "ThreadPool task threw an unknown exception";
        }
    }
}

} // namespace netstream
