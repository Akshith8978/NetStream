#pragma once

#include <unistd.h>
#include <fcntl.h>
#include <utility>
#include <stdexcept>
#include <system_error>

namespace netstream {

/**
 * @brief RAII wrapper for POSIX file descriptors.
 * Move-only semantics, guarantees descriptor cleanup on destruction.
 */
class UniqueFd {
public:
    UniqueFd() noexcept : fd_(-1) {}
    explicit UniqueFd(int fd) noexcept : fd_(fd) {}

    ~UniqueFd() {
        reset();
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset(other.fd_);
            other.fd_ = -1;
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept {
        return fd_;
    }

    [[nodiscard]] bool valid() const noexcept {
        return fd_ >= 0;
    }

    explicit operator bool() const noexcept {
        return valid();
    }

    int release() noexcept {
        int old = fd_;
        fd_ = -1;
        return old;
    }

    void reset(int new_fd = -1) noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = new_fd;
    }

private:
    int fd_{-1};
};

// Utilities for socket file descriptors
bool set_nonblocking(int fd);
bool set_cloexec(int fd);

} // namespace netstream
