#include "udp/jitter_buffer.hpp"

namespace netstream {

JitterBuffer::JitterBuffer(JitterBufferConfig config)
    : config_(config) {}

PushResult JitterBuffer::push(UdpPacket packet, std::chrono::steady_clock::time_point arrival_time) {
    uint32_t seq = packet.header.sequence_number;

    if (!initialized_) {
        initialized_ = true;
        expected_seq_ = seq;
        first_arrival_ = arrival_time;
    }

    // Check if packet arrived after its playout deadline / sequence window has already passed
    if (seq_lt(seq, expected_seq_)) {
        metrics_.packets_late++;
        return PushResult::Late;
    }

    // Check for duplicate packet
    if (buffer_.find(seq) != buffer_.end()) {
        metrics_.packets_duplicate++;
        return PushResult::Duplicate;
    }

    // Check bounded queue capacity
    if (buffer_.size() >= config_.capacity) {
        metrics_.packets_overflow++;
        return PushResult::BufferOverflow;
    }

    // Check if packet arrived out-of-order
    if (seq_diff(seq, expected_seq_) > 0) {
        metrics_.packets_reordered++;
    }

    metrics_.packets_received++;
    metrics_.bytes_received += packet.payload.size();

    BufferedItem item;
    item.packet = std::move(packet);
    item.arrival_time = arrival_time;
    buffer_.emplace(seq, std::move(item));

    return PushResult::Accepted;
}

std::optional<UdpPacket> JitterBuffer::pop(std::chrono::steady_clock::time_point now) {
    if (buffer_.empty()) {
        return std::nullopt;
    }

    // Startup playout buffering delay
    if (!playout_started_) {
        if ((now - first_arrival_ < config_.playout_delay) && buffer_.size() < 8) {
            return std::nullopt;
        }
        playout_started_ = true;
    }

    // Check if expected in-order packet is ready
    auto it = buffer_.find(expected_seq_);
    if (it != buffer_.end()) {
        UdpPacket pkt = std::move(it->second.packet);
        buffer_.erase(it);
        expected_seq_++;
        metrics_.packets_delivered++;
        return pkt;
    }

    // Expected packet is missing: check if deadline has expired
    const auto& earliest_item = buffer_.begin()->second;
    if (now - earliest_item.arrival_time >= config_.packet_timeout) {
        // Timeout expired: declare missing sequence numbers lost and advance
        uint32_t next_available_seq = buffer_.begin()->first;
        int32_t gap = seq_diff(next_available_seq, expected_seq_);
        if (gap > 0) {
            metrics_.packets_lost += static_cast<uint64_t>(gap);
        }

        expected_seq_ = next_available_seq;
        auto next_it = buffer_.begin();
        UdpPacket pkt = std::move(next_it->second.packet);
        buffer_.erase(next_it);
        expected_seq_++;
        metrics_.packets_delivered++;
        return pkt;
    }

    return std::nullopt;
}

std::optional<UdpPacket> JitterBuffer::pop_immediate() {
    if (buffer_.empty()) {
        return std::nullopt;
    }

    auto it = buffer_.begin();
    int32_t gap = seq_diff(it->first, expected_seq_);
    if (gap > 0) {
        metrics_.packets_lost += static_cast<uint64_t>(gap);
    }
    expected_seq_ = it->first;

    UdpPacket pkt = std::move(it->second.packet);
    buffer_.erase(it);
    expected_seq_++;
    metrics_.packets_delivered++;
    return pkt;
}

void JitterBuffer::reset() noexcept {
    buffer_.clear();
    initialized_ = false;
    expected_seq_ = 0;
    playout_started_ = false;
    metrics_ = JitterBufferMetrics{};
}

} // namespace netstream
