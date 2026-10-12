#include "udp/packet.hpp"
#include <cassert>
#include <iostream>
#include <string>

using namespace netstream;

void test_packet_serialization() {
    UdpPacket pkt;
    pkt.header.version = PROTOCOL_VERSION;
    pkt.header.payload_type = PAYLOAD_TYPE_MEDIA;
    pkt.header.flags = FLAG_START_OF_STREAM | FLAG_END_OF_STREAM;
    pkt.header.stream_id = 4242;
    pkt.header.sequence_number = 12345;
    pkt.header.timestamp = 987654;
    pkt.header.file_offset = 1048576;
    pkt.payload = {'N', 'e', 't', 'S', 't', 'r', 'e', 'a', 'm'};
    pkt.header.payload_length = static_cast<uint32_t>(pkt.payload.size());

    auto serialized = pkt.serialize();
    assert(serialized.size() == UDP_PACKET_HEADER_SIZE + pkt.payload.size());

    auto opt = UdpPacket::deserialize(serialized.data(), serialized.size());
    assert(opt.has_value());

    const auto& deserialized = opt.value();
    assert(deserialized.header.version == PROTOCOL_VERSION);
    assert(deserialized.header.payload_type == PAYLOAD_TYPE_MEDIA);
    assert(deserialized.header.is_start_of_stream());
    assert(deserialized.header.is_end_of_stream());
    assert(deserialized.header.stream_id == 4242);
    assert(deserialized.header.sequence_number == 12345);
    assert(deserialized.header.timestamp == 987654);
    assert(deserialized.header.file_offset == 1048576);
    assert(deserialized.payload == pkt.payload);

    std::cout << "[PASS] test_packet_serialization\n";
}

void test_invalid_packet_deserialization() {
    // Too short (< header size)
    uint8_t short_buf[10] = {0};
    auto opt1 = UdpPacket::deserialize(short_buf, sizeof(short_buf));
    assert(!opt1.has_value());

    // Wrong version
    uint8_t wrong_ver[UDP_PACKET_HEADER_SIZE] = {0};
    wrong_ver[0] = 99; // invalid version
    auto opt2 = UdpPacket::deserialize(wrong_ver, sizeof(wrong_ver));
    assert(!opt2.has_value());

    std::cout << "[PASS] test_invalid_packet_deserialization\n";
}

void test_sequence_number_wraparound() {
    uint32_t near_max = 0xFFFFFFF0;
    uint32_t after_wrap = 0x00000005;

    // Modular arithmetic: after_wrap is greater than near_max in sequence order
    assert(seq_lt(near_max, after_wrap));
    assert(!seq_lt(after_wrap, near_max));
    assert(seq_diff(after_wrap, near_max) > 0);

    std::cout << "[PASS] test_sequence_number_wraparound\n";
}

int main() {
    std::cout << "--- Running UDP Packet Unit Tests ---\n";
    test_packet_serialization();
    test_invalid_packet_deserialization();
    test_sequence_number_wraparound();
    std::cout << "All UDP Packet unit tests passed successfully!\n";
    return 0;
}
