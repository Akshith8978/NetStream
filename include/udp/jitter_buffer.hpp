#pragma once

#include "udp/packet.hpp"
#include <map>
#include <chrono>
#include <optional>
#include <cstdint>
#include <cstddef>

namespace netstream {

enum class PushResult {
    Accepted,
    Duplicate,
    Late,
    BufferOverflow
};

struct JitterBufferConfig {
    size_t capacity{512};
    std::chrono::milliseconds playout_delay{50};
    std::chrono::milliseconds packet_timeout{100};
};

struct JitterBufferMetrics {
    uint64_t packets_received{0};
    uint64_t packets_delivered{0};
    uint64_t packets_lost{0};
    uint64_t packets_duplicate{0};
    uint64_t packets_late{0};
    uint64_t packets_reordered{0};
    uint64_t packets_overflow{0};
    uint64_t bytes_received{0};
};

class JitterBuffer {
public:
    explicit JitterBuffer(JitterBufferConfig config = {});

    /**
     * @brief Ingest an incoming UDP packet.
     */
    PushResult push(UdpPacket packet, std::chrono::steady_clock::time_point arrival_time);

    /**
     * @brief Retrieve the next packet in sequence if ready for playout.
     * Advances past missing packets once packet_timeout has elapsed.
     */
    std::optional<UdpPacket> pop(std::chrono::steady_clock::time_point now);

    /**
     * @brief Drain any remaining packets in sequence order, advancing past gaps.
     */
    std::optional<UdpPacket> pop_immediate();

    void reset() noexcept;

    [[nodiscard]] JitterBufferMetrics metrics() const noexcept { return metrics_; }
    [[nodiscard]] size_t size() const noexcept { return buffer_.size(); }
    [[nodiscard]] bool empty() const noexcept { return buffer_.empty(); }
    [[nodiscard]] uint32_t expected_sequence() const noexcept { return expected_seq_; }
    [[nodiscard]] bool is_initialized() const noexcept { return initialized_; }

private:
    struct BufferedItem {
        UdpPacket packet;
        std::chrono::steady_clock::time_point arrival_time;
    };

    struct SeqOrder {
        bool operator()(uint32_t a, uint32_t b) const noexcept {
            return seq_lt(a, b);
        }
    };

    JitterBufferConfig config_;
    JitterBufferMetrics metrics_;

    bool initialized_{false};
    uint32_t expected_seq_{0};
    bool playout_started_{false};
    std::chrono::steady_clock::time_point first_arrival_{};

    std::map<uint32_t, BufferedItem, SeqOrder> buffer_;
};

} // namespace netstream
