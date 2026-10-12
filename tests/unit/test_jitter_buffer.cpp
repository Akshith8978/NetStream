#include "udp/jitter_buffer.hpp"
#include <cassert>
#include <iostream>
#include <chrono>

using namespace netstream;

static UdpPacket make_pkt(uint32_t seq, const std::string& data) {
    UdpPacket pkt;
    pkt.header.version = PROTOCOL_VERSION;
    pkt.header.payload_type = PAYLOAD_TYPE_MEDIA;
    pkt.header.sequence_number = seq;
    pkt.header.payload_length = static_cast<uint32_t>(data.size());
    pkt.payload.assign(data.begin(), data.end());
    return pkt;
}

void test_in_order_delivery() {
    JitterBufferConfig cfg;
    cfg.playout_delay = std::chrono::milliseconds(0);
    JitterBuffer jb(cfg);

    auto now = std::chrono::steady_clock::now();
    assert(jb.push(make_pkt(100, "pkt100"), now) == PushResult::Accepted);
    assert(jb.push(make_pkt(101, "pkt101"), now) == PushResult::Accepted);
    assert(jb.push(make_pkt(102, "pkt102"), now) == PushResult::Accepted);

    auto p1 = jb.pop(now);
    assert(p1.has_value() && p1->header.sequence_number == 100);

    auto p2 = jb.pop(now);
    assert(p2.has_value() && p2->header.sequence_number == 101);

    auto p3 = jb.pop(now);
    assert(p3.has_value() && p3->header.sequence_number == 102);

    assert(!jb.pop(now).has_value());
    assert(jb.metrics().packets_delivered == 3);
    assert(jb.metrics().packets_lost == 0);
    std::cout << "[PASS] test_in_order_delivery\n";
}

void test_out_of_order_reordering() {
    JitterBufferConfig cfg;
    cfg.playout_delay = std::chrono::milliseconds(0);
    JitterBuffer jb(cfg);

    auto now = std::chrono::steady_clock::now();
    assert(jb.push(make_pkt(100, "pkt100"), now) == PushResult::Accepted);
    assert(jb.push(make_pkt(102, "pkt102"), now) == PushResult::Accepted); // out of order!
    assert(jb.push(make_pkt(101, "pkt101"), now) == PushResult::Accepted);

    assert(jb.metrics().packets_reordered >= 1);

    // Should pop in exact sequence: 100, 101, 102
    auto p1 = jb.pop(now);
    assert(p1.has_value() && p1->header.sequence_number == 100);

    auto p2 = jb.pop(now);
    assert(p2.has_value() && p2->header.sequence_number == 101);

    auto p3 = jb.pop(now);
    assert(p3.has_value() && p3->header.sequence_number == 102);

    assert(!jb.pop(now).has_value());
    std::cout << "[PASS] test_out_of_order_reordering\n";
}

void test_duplicates_and_late_packets() {
    JitterBufferConfig cfg;
    cfg.playout_delay = std::chrono::milliseconds(0);
    JitterBuffer jb(cfg);

    auto now = std::chrono::steady_clock::now();
    assert(jb.push(make_pkt(10, "first"), now) == PushResult::Accepted);
    assert(jb.push(make_pkt(10, "duplicate"), now) == PushResult::Duplicate);
    assert(jb.metrics().packets_duplicate == 1);

    // Pop sequence 10
    auto p = jb.pop(now);
    assert(p.has_value() && p->header.sequence_number == 10);

    // Now sequence 10 is late
    assert(jb.push(make_pkt(10, "late"), now) == PushResult::Late);
    assert(jb.metrics().packets_late == 1);

    std::cout << "[PASS] test_duplicates_and_late_packets\n";
}

void test_missing_packet_deadline_timeout() {
    JitterBufferConfig cfg;
    cfg.playout_delay = std::chrono::milliseconds(0);
    cfg.packet_timeout = std::chrono::milliseconds(50);
    JitterBuffer jb(cfg);

    auto t0 = std::chrono::steady_clock::now();
    // Packet 1 arrives at t0
    jb.push(make_pkt(1, "one"), t0);
    // Packet 3 arrives at t0 (packet 2 is missing!)
    jb.push(make_pkt(3, "three"), t0);

    // Pop packet 1
    auto p1 = jb.pop(t0);
    assert(p1.has_value() && p1->header.sequence_number == 1);

    // At t0, packet 2 is missing and hasn't timed out yet
    auto p2 = jb.pop(t0);
    assert(!p2.has_value());

    // Advance time past packet_timeout (t0 + 60ms)
    auto t1 = t0 + std::chrono::milliseconds(60);
    auto p3 = jb.pop(t1);

    // Packet 2 timed out, declared lost, and packet 3 is played out!
    assert(p3.has_value() && p3->header.sequence_number == 3);
    assert(jb.metrics().packets_lost == 1);
    assert(jb.metrics().packets_delivered == 2);

    std::cout << "[PASS] test_missing_packet_deadline_timeout\n";
}

int main() {
    std::cout << "--- Running JitterBuffer Unit Tests ---\n";
    test_in_order_delivery();
    test_out_of_order_reordering();
    test_duplicates_and_late_packets();
    test_missing_packet_deadline_timeout();
    std::cout << "All JitterBuffer unit tests passed successfully!\n";
    return 0;
}
