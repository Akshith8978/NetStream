#pragma once

#include "common/fd.hpp"
#include "udp/packet.hpp"
#include "udp/jitter_buffer.hpp"
#include <string>
#include <chrono>
#include <cstdint>
#include <cstddef>

namespace netstream {

struct UdpSenderConfig {
    std::string target_host{"127.0.0.1"};
    uint16_t target_port{9000};
    size_t chunk_size{DEFAULT_MAX_CHUNK_SIZE};
    std::chrono::microseconds pacing_interval{1000}; // 1ms default
    uint32_t stream_id{1001};
    uint32_t initial_seq{0};
};

struct UdpSenderStats {
    uint64_t packets_sent{0};
    uint64_t bytes_sent{0};
    uint64_t file_size{0};
    std::string sha256;
    std::chrono::microseconds duration{0};
    double throughput_mbps{0.0};
};

class UdpSender {
public:
    explicit UdpSender(UdpSenderConfig config);

    /**
     * @brief Stream a local file over UDP datagrams.
     * @param filepath Path to input file.
     * @return Transmission statistics including SHA-256 and throughput.
     */
    UdpSenderStats send_file(const std::string& filepath);

private:
    UdpSenderConfig config_;
};

struct UdpReceiverConfig {
    std::string bind_host{"0.0.0.0"};
    uint16_t bind_port{9000};
    JitterBufferConfig jitter_config{};
    std::chrono::milliseconds idle_timeout{3000};
};

struct UdpReceiverStats {
    uint64_t packets_received{0};
    uint64_t packets_delivered{0};
    uint64_t packets_lost{0};
    uint64_t packets_duplicate{0};
    uint64_t packets_late{0};
    uint64_t packets_reordered{0};
    uint64_t bytes_written{0};
    uint64_t total_expected_bytes{0};
    bool is_complete{false};
    std::string sha256;
    std::chrono::microseconds duration{0};
    double goodput_mbps{0.0};
};

class UdpReceiver {
public:
    explicit UdpReceiver(UdpReceiverConfig config);

    /**
     * @brief Receive a streamed file and reconstruct it on disk.
     * @param output_filepath Destination path for reconstructed file.
     * @param expected_stream_id If >0, filters packets belonging to this stream only.
     * @return Reception statistics, completeness status and SHA-256.
     */
    UdpReceiverStats receive_file(const std::string& output_filepath, uint32_t expected_stream_id = 0);

private:
    UdpReceiverConfig config_;
};

} // namespace netstream
