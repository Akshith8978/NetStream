#include "common/protocol.hpp"
#include <cstring>
#include <algorithm>

namespace netstream {

std::vector<uint8_t> serialize_frame(const uint8_t* data, size_t length) {
    std::vector<uint8_t> out(FRAME_HEADER_SIZE + length);
    uint32_t net_len = htonl(static_cast<uint32_t>(length));
    std::memcpy(out.data(), &net_len, FRAME_HEADER_SIZE);
    if (length > 0 && data != nullptr) {
        std::memcpy(out.data() + FRAME_HEADER_SIZE, data, length);
    }
    return out;
}

std::vector<uint8_t> serialize_frame(const std::vector<uint8_t>& payload) {
    return serialize_frame(payload.data(), payload.size());
}

std::vector<uint8_t> serialize_frame(std::string_view payload) {
    return serialize_frame(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
}

void FrameParser::append(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) return;
    compact_buffer();
    buffer_.insert(buffer_.end(), data, data + length);
}

ParseStatus FrameParser::next_frame(Frame& out_frame) {
    if (buffered_bytes() < FRAME_HEADER_SIZE) {
        return ParseStatus::NeedMoreData;
    }

    uint32_t net_len = 0;
    std::memcpy(&net_len, buffer_.data() + read_pos_, FRAME_HEADER_SIZE);
    uint32_t host_len = ntohl(net_len);

    if (host_len > max_message_size_) {
        return ParseStatus::FrameTooLarge;
    }

    size_t total_required = FRAME_HEADER_SIZE + static_cast<size_t>(host_len);
    if (buffered_bytes() < total_required) {
        return ParseStatus::NeedMoreData;
    }

    // Extract payload
    out_frame.payload.assign(
        buffer_.begin() + static_cast<ptrdiff_t>(read_pos_ + FRAME_HEADER_SIZE),
        buffer_.begin() + static_cast<ptrdiff_t>(read_pos_ + total_required)
    );

    read_pos_ += total_required;
    return ParseStatus::Ok;
}

void FrameParser::reset() noexcept {
    buffer_.clear();
    read_pos_ = 0;
}

void FrameParser::compact_buffer() {
    // If read_pos_ consumed a large fraction of the buffer, shift unread data to beginning
    if (read_pos_ > 0 && (read_pos_ >= 4096 || read_pos_ >= buffer_.size() / 2)) {
        if (read_pos_ < buffer_.size()) {
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<ptrdiff_t>(read_pos_));
        } else {
            buffer_.clear();
        }
        read_pos_ = 0;
    }
}

} // namespace netstream
