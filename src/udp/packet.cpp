#include "udp/packet.hpp"
#include <arpa/inet.h>
#include <endian.h>
#include <cstring>

namespace netstream {

std::vector<uint8_t> UdpPacket::serialize() const {
    std::vector<uint8_t> buffer(UDP_PACKET_HEADER_SIZE + payload.size());
    uint8_t* p = buffer.data();

    // 1 byte version
    *p++ = header.version;

    // 1 byte payload type
    *p++ = header.payload_type;

    // 2 bytes flags
    uint16_t net_flags = htons(header.flags);
    std::memcpy(p, &net_flags, sizeof(net_flags));
    p += sizeof(net_flags);

    // 4 bytes stream id
    uint32_t net_sid = htonl(header.stream_id);
    std::memcpy(p, &net_sid, sizeof(net_sid));
    p += sizeof(net_sid);

    // 4 bytes sequence number
    uint32_t net_seq = htonl(header.sequence_number);
    std::memcpy(p, &net_seq, sizeof(net_seq));
    p += sizeof(net_seq);

    // 4 bytes timestamp
    uint32_t net_ts = htonl(header.timestamp);
    std::memcpy(p, &net_ts, sizeof(net_ts));
    p += sizeof(net_ts);

    // 4 bytes payload length
    uint32_t net_len = htonl(static_cast<uint32_t>(payload.size()));
    std::memcpy(p, &net_len, sizeof(net_len));
    p += sizeof(net_len);

    // 8 bytes file offset
    uint64_t net_off = htobe64(header.file_offset);
    std::memcpy(p, &net_off, sizeof(net_off));
    p += sizeof(net_off);

    // Payload bytes
    if (!payload.empty()) {
        std::memcpy(p, payload.data(), payload.size());
    }

    return buffer;
}

std::optional<UdpPacket> UdpPacket::deserialize(const uint8_t* data, size_t length) {
    if (data == nullptr || length < UDP_PACKET_HEADER_SIZE) {
        return std::nullopt;
    }

    const uint8_t* p = data;
    UdpPacket pkt;

    pkt.header.version = *p++;
    if (pkt.header.version != PROTOCOL_VERSION) {
        return std::nullopt;
    }

    pkt.header.payload_type = *p++;

    uint16_t net_flags = 0;
    std::memcpy(&net_flags, p, sizeof(net_flags));
    pkt.header.flags = ntohs(net_flags);
    p += sizeof(net_flags);

    uint32_t net_sid = 0;
    std::memcpy(&net_sid, p, sizeof(net_sid));
    pkt.header.stream_id = ntohl(net_sid);
    p += sizeof(net_sid);

    uint32_t net_seq = 0;
    std::memcpy(&net_seq, p, sizeof(net_seq));
    pkt.header.sequence_number = ntohl(net_seq);
    p += sizeof(net_seq);

    uint32_t net_ts = 0;
    std::memcpy(&net_ts, p, sizeof(net_ts));
    pkt.header.timestamp = ntohl(net_ts);
    p += sizeof(net_ts);

    uint32_t net_len = 0;
    std::memcpy(&net_len, p, sizeof(net_len));
    pkt.header.payload_length = ntohl(net_len);
    p += sizeof(net_len);

    uint64_t net_off = 0;
    std::memcpy(&net_off, p, sizeof(net_off));
    pkt.header.file_offset = be64toh(net_off);
    p += sizeof(net_off);

    // Verify payload length against remaining buffer size
    size_t remaining = length - UDP_PACKET_HEADER_SIZE;
    if (remaining < pkt.header.payload_length) {
        return std::nullopt;
    }

    pkt.payload.assign(p, p + pkt.header.payload_length);
    return pkt;
}

} // namespace netstream
