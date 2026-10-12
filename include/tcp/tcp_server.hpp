#pragma once

#include "common/fd.hpp"
#include "common/protocol.hpp"
#include "tcp/connection.hpp"
#include "tcp/thread_pool.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>
#include <sys/epoll.h>

namespace netstream {

struct ServerConfig {
    std::string host{"0.0.0.0"};
    uint16_t port{8080};
    size_t worker_threads{4};
    size_t queue_capacity{1024};
    uint32_t max_message_size{DEFAULT_MAX_MESSAGE_SIZE};
    int backlog{128};
};

struct WorkerResponse {
    uint64_t conn_id;
    uint32_t generation;
    std::vector<uint8_t> response_payload;
};

class TcpServer {
public:
    explicit TcpServer(ServerConfig config);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    /**
     * @brief Initialize listening socket, epoll, and signalfd.
     * @return true if initialized successfully, false otherwise.
     */
    bool init();

    /**
     * @brief Run the single-threaded epoll reactor loop. Blocks until stopped.
     */
    void run();

    /**
     * @brief Signal the reactor to stop gracefully. Safe to call from any thread.
     */
    void stop();

    [[nodiscard]] bool is_running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint16_t bound_port() const noexcept {
        return bound_port_;
    }

    [[nodiscard]] size_t active_connections() const noexcept;

private:
    void handle_accept();
    void handle_client_read(int fd);
    void handle_client_write(int fd);
    void handle_worker_completions();
    void handle_signals();
    void close_connection(int fd, const std::string& reason);
    void update_epoll(int fd, uint32_t events);

    ServerConfig config_;
    uint16_t bound_port_{0};
    UniqueFd listen_fd_;
    UniqueFd epoll_fd_;
    UniqueFd signal_fd_;
    UniqueFd completion_event_fd_;

    std::atomic<bool> running_{false};
    uint64_t next_conn_id_{1};

    // Connections are owned strictly by the Reactor thread
    std::unordered_map<int, Connection> connections_;
    std::unordered_map<uint64_t, int> conn_id_to_fd_;

    ThreadPool thread_pool_;

    // Completion queue for worker threads delivering responses to the reactor
    std::mutex completion_mutex_;
    std::vector<WorkerResponse> completion_queue_;
};

} // namespace netstream
