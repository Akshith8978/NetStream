#include "tcp/connection.hpp"
#include "common/logger.hpp"
#include <sys/socket.h>
#include <cerrno>

namespace netstream {

Connection::Connection(uint64_t id, uint32_t generation, UniqueFd fd, std::string peer_ip, uint16_t peer_port,
                       uint32_t max_message_size)
    : id_(id),
      generation_(generation),
      fd_(std::move(fd)),
      peer_ip_(std::move(peer_ip)),
      peer_port_(peer_port),
      parser_(max_message_size) {}

void Connection::queue_output(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) return;
    // Compact output buffer if already drained offset is large
    if (output_offset_ > 0 && output_offset_ == output_buffer_.size()) {
        output_buffer_.clear();
        output_offset_ = 0;
    } else if (output_offset_ > 4096 && output_offset_ >= output_buffer_.size() / 2) {
        output_buffer_.erase(output_buffer_.begin(), output_buffer_.begin() + static_cast<ptrdiff_t>(output_offset_));
        output_offset_ = 0;
    }
    output_buffer_.insert(output_buffer_.end(), data, data + length);
}

void Connection::queue_output(const std::vector<uint8_t>& bytes) {
    queue_output(bytes.data(), bytes.size());
}

bool Connection::has_pending_output() const noexcept {
    return output_offset_ < output_buffer_.size();
}

int Connection::flush_output() {
    while (has_pending_output()) {
        const uint8_t* send_ptr = output_buffer_.data() + output_offset_;
        size_t bytes_to_send = output_buffer_.size() - output_offset_;

        ssize_t n = ::send(fd_.get(), send_ptr, bytes_to_send, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket output buffer full, wait for next EPOLLOUT
                return 0;
            }
            LOG_DEBUG << "Socket send error on connection " << id_ << ": errno " << errno;
            return -1;
        }

        output_offset_ += static_cast<size_t>(n);
    }

    // Completely drained
    output_buffer_.clear();
    output_offset_ = 0;
    return 0;
}

} // namespace netstream
