#include "tcp/tcp_server.hpp"
#include "common/logger.hpp"
#include <sys/socket.h>
#include <sys/signalfd.h>
#include <sys/eventfd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

namespace netstream {

TcpServer::TcpServer(ServerConfig config)
    : config_(std::move(config)),
      thread_pool_(config_.worker_threads, config_.queue_capacity) {}

TcpServer::~TcpServer() {
    stop();
}

bool TcpServer::init() {
    // 1. Block SIGINT and SIGTERM on this thread and create signalfd
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &mask, nullptr) != 0) {
        LOG_ERROR << "Failed to block signals: " << strerror(errno);
        return false;
    }

    int sfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sfd < 0) {
        LOG_ERROR << "signalfd failed: " << strerror(errno);
        return false;
    }
    signal_fd_.reset(sfd);

    // 2. Create completion eventfd
    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (efd < 0) {
        LOG_ERROR << "eventfd failed: " << strerror(errno);
        return false;
    }
    completion_event_fd_.reset(efd);

    // 3. Create listening TCP socket
    int lfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (lfd < 0) {
        LOG_ERROR << "Socket creation failed: " << strerror(errno);
        return false;
    }
    listen_fd_.reset(lfd);

    int opt = 1;
    if (setsockopt(listen_fd_.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_WARN << "setsockopt SO_REUSEADDR failed: " << strerror(errno);
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(config_.port);
    if (inet_pton(AF_INET, config_.host.c_str(), &server_addr.sin_addr) <= 0) {
        LOG_ERROR << "Invalid listen host address: " << config_.host;
        return false;
    }

    if (bind(listen_fd_.get(), reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        LOG_ERROR << "Bind failed on " << config_.host << ":" << config_.port << ": " << strerror(errno);
        return false;
    }

    if (listen(listen_fd_.get(), config_.backlog) < 0) {
        LOG_ERROR << "Listen failed: " << strerror(errno);
        return false;
    }

    // Determine actual bound port
    sockaddr_in bound_addr{};
    socklen_t addr_len = sizeof(bound_addr);
    if (getsockname(listen_fd_.get(), reinterpret_cast<sockaddr*>(&bound_addr), &addr_len) == 0) {
        bound_port_ = ntohs(bound_addr.sin_port);
    } else {
        bound_port_ = config_.port;
    }

    // 4. Create epoll instance
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        LOG_ERROR << "epoll_create1 failed: " << strerror(errno);
        return false;
    }
    epoll_fd_.reset(epfd);

    // Register listening socket with EPOLLET
    epoll_event ev_listen{};
    ev_listen.events = EPOLLIN | EPOLLET;
    ev_listen.data.fd = listen_fd_.get();
    if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, listen_fd_.get(), &ev_listen) < 0) {
        LOG_ERROR << "epoll_ctl ADD listen_fd failed: " << strerror(errno);
        return false;
    }

    // Register signalfd
    epoll_event ev_sig{};
    ev_sig.events = EPOLLIN;
    ev_sig.data.fd = signal_fd_.get();
    if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, signal_fd_.get(), &ev_sig) < 0) {
        LOG_ERROR << "epoll_ctl ADD signal_fd failed: " << strerror(errno);
        return false;
    }

    // Register completion eventfd
    epoll_event ev_comp{};
    ev_comp.events = EPOLLIN;
    ev_comp.data.fd = completion_event_fd_.get();
    if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, completion_event_fd_.get(), &ev_comp) < 0) {
        LOG_ERROR << "epoll_ctl ADD completion_event_fd failed: " << strerror(errno);
        return false;
    }

    LOG_INFO << "TcpServer initialized successfully on " << config_.host << ":" << bound_port_;
    return true;
}

void TcpServer::run() {
    running_.store(true, std::memory_order_release);
    constexpr int MAX_EVENTS = 64;
    epoll_event events[MAX_EVENTS];

    LOG_INFO << "TcpServer entering reactor loop...";

    while (running_.load(std::memory_order_acquire)) {
        int nfds = epoll_wait(epoll_fd_.get(), events, MAX_EVENTS, -1);
        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOG_ERROR << "epoll_wait failed: " << strerror(errno);
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == signal_fd_.get()) {
                handle_signals();
            } else if (fd == completion_event_fd_.get()) {
                handle_worker_completions();
            } else if (fd == listen_fd_.get()) {
                handle_accept();
            } else {
                // Client connection event
                if (ev & (EPOLLERR | EPOLLHUP)) {
                    close_connection(fd, "EPOLLERR or EPOLLHUP received");
                    continue;
                }

                if (ev & EPOLLIN) {
                    handle_client_read(fd);
                }

                if (ev & EPOLLOUT) {
                    handle_client_write(fd);
                }

                if (ev & EPOLLRDHUP) {
                    auto it = connections_.find(fd);
                    if (it != connections_.end()) {
                        if (!it->second.has_pending_output()) {
                            close_connection(fd, "Client closed connection (EPOLLRDHUP)");
                        } else {
                            it->second.mark_closing();
                        }
                    }
                }
            }
        }
    }

    LOG_INFO << "TcpServer exiting reactor loop, starting graceful teardown...";

    // Deregister listen_fd so no new connections are accepted
    if (listen_fd_.valid() && epoll_fd_.valid()) {
        epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, listen_fd_.get(), nullptr);
    }

    // Flush and close remaining client connections
    for (auto& [fd, conn] : connections_) {
        conn.flush_output();
        if (epoll_fd_.valid()) {
            epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr);
        }
    }
    connections_.clear();
    conn_id_to_fd_.clear();

    // Shut down worker pool and drain pending work
    thread_pool_.shutdown(true);
    LOG_INFO << "TcpServer shutdown complete.";
}

void TcpServer::stop() {
    if (running_.exchange(false, std::memory_order_acq_rel)) {
        // Wake up epoll_wait
        if (completion_event_fd_.valid()) {
            uint64_t val = 1;
            [[maybe_unused]] ssize_t res = ::write(completion_event_fd_.get(), &val, sizeof(val));
        }
    }
}

size_t TcpServer::active_connections() const noexcept {
    return connections_.size();
}

void TcpServer::handle_accept() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept4(listen_fd_.get(), reinterpret_cast<sockaddr*>(&client_addr), &client_len,
                                SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // All pending connections accepted (edge-triggered drain complete)
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            LOG_WARN << "accept4 error: " << strerror(errno);
            break;
        }

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        uint16_t client_port = ntohs(client_addr.sin_port);

        uint64_t conn_id = next_conn_id_++;
        UniqueFd ufd(client_fd);

        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
        ev.data.fd = client_fd;
        if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, client_fd, &ev) < 0) {
            LOG_ERROR << "Failed to register client fd in epoll: " << strerror(errno);
            continue;
        }

        connections_.emplace(client_fd, Connection(conn_id, 1, std::move(ufd), ip_str, client_port, config_.max_message_size));
        conn_id_to_fd_[conn_id] = client_fd;

        LOG_DEBUG << "Accepted connection " << conn_id << " from " << ip_str << ":" << client_port;
    }
}

void TcpServer::handle_client_read(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;

    Connection& conn = it->second;
    uint8_t read_buf[8192];

    while (true) {
        ssize_t n = ::recv(fd, read_buf, sizeof(read_buf), 0);
        if (n > 0) {
            conn.parser().append(read_buf, static_cast<size_t>(n));

            while (true) {
                Frame frame;
                ParseStatus status = conn.parser().next_frame(frame);
                if (status == ParseStatus::Ok) {
                    uint64_t conn_id = conn.id();
                    uint32_t generation = conn.generation();
                    std::vector<uint8_t> payload = std::move(frame.payload);

                    // Offload processing to worker thread pool
                    bool queued = thread_pool_.submit([this, conn_id, generation, payload = std::move(payload)]() mutable {
                        // Echo worker task
                        WorkerResponse resp;
                        resp.conn_id = conn_id;
                        resp.generation = generation;
                        resp.response_payload = std::move(payload);

                        {
                            std::lock_guard<std::mutex> lock(completion_mutex_);
                            completion_queue_.push_back(std::move(resp));
                        }

                        // Wake epoll reactor via completion eventfd
                        uint64_t val = 1;
                        [[maybe_unused]] ssize_t w = ::write(completion_event_fd_.get(), &val, sizeof(val));
                    }, FullQueuePolicy::Reject);

                    if (!queued) {
                        LOG_WARN << "Thread pool queue saturated, closing connection " << conn_id;
                        close_connection(fd, "Task queue full");
                        return;
                    }
                } else if (status == ParseStatus::NeedMoreData) {
                    break;
                } else {
                    LOG_WARN << "Protocol error on connection " << conn.id() 
                             << " (code " << static_cast<int>(status) << "), closing";
                    close_connection(fd, "Protocol parse error");
                    return;
                }
            }
        } else if (n == 0) {
            // Client closed connection
            close_connection(fd, "Client disconnected");
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket fully drained for edge-triggered read
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            LOG_DEBUG << "recv error on connection " << conn.id() << ": " << strerror(errno);
            close_connection(fd, "recv error");
            return;
        }
    }
}

void TcpServer::handle_client_write(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;

    Connection& conn = it->second;
    if (conn.flush_output() < 0) {
        close_connection(fd, "flush_output error");
        return;
    }

    if (!conn.has_pending_output()) {
        // Output drained: remove EPOLLOUT to prevent busy epoll spinning
        update_epoll(fd, EPOLLIN | EPOLLRDHUP | EPOLLET);

        if (conn.is_closing()) {
            close_connection(fd, "Connection drained after closing signal");
        }
    }
}

void TcpServer::handle_worker_completions() {
    uint64_t val = 0;
    [[maybe_unused]] ssize_t r = ::read(completion_event_fd_.get(), &val, sizeof(val));

    std::vector<WorkerResponse> completions;
    {
        std::lock_guard<std::mutex> lock(completion_mutex_);
        completions.swap(completion_queue_);
    }

    for (auto& resp : completions) {
        auto id_it = conn_id_to_fd_.find(resp.conn_id);
        if (id_it == conn_id_to_fd_.end()) {
            // Connection closed while task was executing, ignore response safely
            continue;
        }

        int fd = id_it->second;
        auto conn_it = connections_.find(fd);
        if (conn_it == connections_.end()) {
            continue;
        }

        Connection& conn = conn_it->second;
        if (conn.generation() != resp.generation) {
            // Stale response from previous connection generation on reused fd, discard
            continue;
        }

        // Frame the payload and queue to output buffer
        std::vector<uint8_t> framed = serialize_frame(resp.response_payload);
        conn.queue_output(framed);

        // Attempt non-blocking write
        if (conn.flush_output() < 0) {
            close_connection(fd, "Write error during completion flush");
            continue;
        }

        if (conn.has_pending_output()) {
            // Need EPOLLOUT notification to finish sending remaining bytes
            update_epoll(fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET);
        }
    }
}

void TcpServer::handle_signals() {
    signalfd_siginfo fdsi{};
    ssize_t s = ::read(signal_fd_.get(), &fdsi, sizeof(fdsi));
    if (s == sizeof(fdsi)) {
        LOG_INFO << "Received signal " << fdsi.ssi_signo << " (SIGINT/SIGTERM), initiating graceful shutdown";
        stop();
    }
}

void TcpServer::close_connection(int fd, const std::string& reason) {
    auto it = connections_.find(fd);
    if (it != connections_.end()) {
        LOG_DEBUG << "Closing connection " << it->second.id() << ": " << reason;
        conn_id_to_fd_.erase(it->second.id());
        if (epoll_fd_.valid()) {
            epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr);
        }
        connections_.erase(it);
    }
}

void TcpServer::update_epoll(int fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(epoll_fd_.get(), EPOLL_CTL_MOD, fd, &ev);
}

} // namespace netstream
