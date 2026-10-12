#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include <optional>
#include <arpa/inet.h>

namespace netstream {

constexpr uint32_t DEFAULT_MAX_MESSAGE_SIZE = 16 * 1024 * 1024; // 16 MB
constexpr size_t FRAME_HEADER_SIZE = sizeof(uint32_t); // 4 bytes

enum class ParseStatus {
    Ok,
    NeedMoreData,
    FrameTooLarge,
    InvalidHeader
};

/**
 * @brief Binary-safe frame: 4-byte network-order length prefix followed by raw payload.
 */
struct Frame {
    std::vector<uint8_t> payload;

    [[nodiscard]] size_t size() const noexcept {
        return payload.size();
    }

    [[nodiscard]] std::string as_string() const {
        return std::string(reinterpret_cast<const char*>(payload.data()), payload.size());
    }

    static Frame from_string(std::string_view sv) {
        Frame f;
        f.payload.assign(sv.begin(), sv.end());
        return f;
    }
};

/**
 * @brief Serializes a payload into a framed byte stream with a network-order length prefix.
 */
std::vector<uint8_t> serialize_frame(const uint8_t* data, size_t length);
std::vector<uint8_t> serialize_frame(const std::vector<uint8_t>& payload);
std::vector<uint8_t> serialize_frame(std::string_view payload);

/**
 * @brief Incremental stream parser for length-prefixed binary frames.
 * Preserves incomplete frames across multiple reads and yields multiple complete frames.
 */
class FrameParser {
public:
    explicit FrameParser(uint32_t max_message_size = DEFAULT_MAX_MESSAGE_SIZE)
        : max_message_size_(max_message_size) {}

    /**
     * @brief Append newly received raw bytes from the network.
     */
    void append(const uint8_t* data, size_t length);

    /**
     * @brief Attempt to extract the next complete frame.
     * @param out_frame Populated with the frame if Ok is returned.
     * @return ParseStatus::Ok if a complete frame was parsed,
     *         ParseStatus::NeedMoreData if more bytes are required,
     *         ParseStatus::FrameTooLarge if length exceeds max_message_size.
     */
    ParseStatus next_frame(Frame& out_frame);

    /**
     * @brief Reset parser state and clear buffer.
     */
    void reset() noexcept;

    [[nodiscard]] size_t buffered_bytes() const noexcept {
        return buffer_.size() - read_pos_;
    }

    [[nodiscard]] uint32_t max_message_size() const noexcept {
        return max_message_size_;
    }

private:
    void compact_buffer();

    uint32_t max_message_size_;
    std::vector<uint8_t> buffer_;
    size_t read_pos_{0};
};

} // namespace netstream
