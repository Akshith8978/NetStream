#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include <string>

namespace netstream {

constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr uint8_t PAYLOAD_TYPE_MEDIA = 1;
constexpr uint8_t PAYLOAD_TYPE_CONTROL = 2;

constexpr uint16_t FLAG_START_OF_STREAM = 0x0001;
constexpr uint16_t FLAG_END_OF_STREAM   = 0x0002;

constexpr size_t UDP_PACKET_HEADER_SIZE = 28;
constexpr size_t DEFAULT_MAX_CHUNK_SIZE = 1400; // Leaves plenty of margin for MTU 1500

struct UdpHeader {
    uint8_t  version{PROTOCOL_VERSION};
    uint8_t  payload_type{PAYLOAD_TYPE_MEDIA};
    uint16_t flags{0};
    uint32_t stream_id{0};
    uint32_t sequence_number{0};
    uint32_t timestamp{0};
    uint32_t payload_length{0};
    uint64_t file_offset{0};

    [[nodiscard]] bool is_start_of_stream() const noexcept {
        return (flags & FLAG_START_OF_STREAM) != 0;
    }

    [[nodiscard]] bool is_end_of_stream() const noexcept {
        return (flags & FLAG_END_OF_STREAM) != 0;
    }
};

struct UdpPacket {
    UdpHeader header;
    std::vector<uint8_t> payload;

    /**
     * @brief Serialize this packet to network byte order bytes.
     */
    [[nodiscard]] std::vector<uint8_t> serialize() const;

    /**
     * @brief Parse packet from network datagram.
     */
    static std::optional<UdpPacket> deserialize(const uint8_t* data, size_t length);
};

// Modular 32-bit sequence number comparison
inline bool seq_lt(uint32_t a, uint32_t b) noexcept {
    return static_cast<int32_t>(a - b) < 0;
}

inline bool seq_le(uint32_t a, uint32_t b) noexcept {
    return static_cast<int32_t>(a - b) <= 0;
}

inline int32_t seq_diff(uint32_t a, uint32_t b) noexcept {
    return static_cast<int32_t>(a - b);
}

} // namespace netstream
