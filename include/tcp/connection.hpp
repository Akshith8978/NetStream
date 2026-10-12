#pragma once

#include "common/fd.hpp"
#include "common/protocol.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace netstream {

class Connection {
public:
    Connection(uint64_t id, uint32_t generation, UniqueFd fd, std::string peer_ip, uint16_t peer_port,
               uint32_t max_message_size = DEFAULT_MAX_MESSAGE_SIZE);

    ~Connection() = default;

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    Connection(Connection&&) noexcept = default;
    Connection& operator=(Connection&&) noexcept = default;

    [[nodiscard]] uint64_t id() const noexcept { return id_; }
    [[nodiscard]] uint32_t generation() const noexcept { return generation_; }
    [[nodiscard]] int fd() const noexcept { return fd_.get(); }
    [[nodiscard]] const std::string& peer_ip() const noexcept { return peer_ip_; }
    [[nodiscard]] uint16_t peer_port() const noexcept { return peer_port_; }
    [[nodiscard]] bool is_closing() const noexcept { return is_closing_; }

    void mark_closing() noexcept { is_closing_ = true; }

    FrameParser& parser() noexcept { return parser_; }

    /**
     * @brief Append a framed response into the connection's output buffer.
     */
    void queue_output(const uint8_t* data, size_t length);
    void queue_output(const std::vector<uint8_t>& bytes);

    /**
     * @brief True if there is pending data waiting to be sent.
     */
    [[nodiscard]] bool has_pending_output() const noexcept;

    /**
     * @brief Attempt to flush pending output to the socket using non-blocking send().
     * @return 0 on success (all flushed or blocked on EAGAIN), -1 on fatal socket error.
     */
    int flush_output();

private:
    uint64_t id_;
    uint32_t generation_;
    UniqueFd fd_;
    std::string peer_ip_;
    uint16_t peer_port_;
    bool is_closing_{false};

    FrameParser parser_;
    std::vector<uint8_t> output_buffer_;
    size_t output_offset_{0};
};

} // namespace netstream
