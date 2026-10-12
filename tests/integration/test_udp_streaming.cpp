#include "udp/streamer.hpp"
#include "common/sha256.hpp"
#include "common/logger.hpp"
#include <cassert>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>

using namespace netstream;

void create_test_file(const std::string& path, size_t size_bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    for (size_t i = 0; i < size_bytes; ++i) {
        char ch = static_cast<char>((i * 7 + 13) & 0xFF);
        out.put(ch);
    }
}

int main() {
    std::cout << "--- Running UDP Streaming Integration Test ---\n";
    Logger::instance().set_level(LogLevel::WARN);

    std::string src_path = "/tmp/netstream_test_src.bin";
    std::string dst_path = "/tmp/netstream_test_dst.bin";
    size_t test_size = 128 * 1024; // 128 KB
    create_test_file(src_path, test_size);

    std::string original_hash = Sha256::hash_file(src_path);

    UdpReceiverConfig rx_cfg;
    rx_cfg.bind_host = "127.0.0.1";
    rx_cfg.bind_port = 9876;
    rx_cfg.jitter_config.playout_delay = std::chrono::milliseconds(20);
    rx_cfg.jitter_config.packet_timeout = std::chrono::milliseconds(50);
    rx_cfg.idle_timeout = std::chrono::milliseconds(500);

    UdpReceiver receiver(rx_cfg);
    UdpReceiverStats rx_stats;

    std::thread rx_thread([&]() {
        rx_stats = receiver.receive_file(dst_path, 7777);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    UdpSenderConfig tx_cfg;
    tx_cfg.target_host = "127.0.0.1";
    tx_cfg.target_port = 9876;
    tx_cfg.stream_id = 7777;
    tx_cfg.chunk_size = 1200;
    tx_cfg.pacing_interval = std::chrono::microseconds(200);

    UdpSender sender(tx_cfg);
    auto tx_stats = sender.send_file(src_path);

    if (rx_thread.joinable()) {
        rx_thread.join();
    }

    assert(tx_stats.sha256 == original_hash);
    assert(rx_stats.is_complete);
    assert(rx_stats.bytes_written == test_size);
    assert(rx_stats.sha256 == original_hash);
    assert(rx_stats.packets_lost == 0);

    std::cout << "[PASS] 128KB file streamed over UDP with zero loss, exact SHA-256 match!\n";
    std::cout << "Original Hash:     " << original_hash << "\n";
    std::cout << "Reconstructed Hash:" << rx_stats.sha256 << "\n";

    // Test empty file
    std::string empty_src = "/tmp/netstream_empty_src.bin";
    std::string empty_dst = "/tmp/netstream_empty_dst.bin";
    create_test_file(empty_src, 0);

    rx_cfg.bind_port = 9877;
    UdpReceiver rx_empty(rx_cfg);
    UdpReceiverStats rx_empty_stats;

    std::thread rx_empty_th([&]() {
        rx_empty_stats = rx_empty.receive_file(empty_dst, 8888);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    tx_cfg.target_port = 9877;
    tx_cfg.stream_id = 8888;
    UdpSender tx_empty(tx_cfg);
    auto tx_empty_stats = tx_empty.send_file(empty_src);

    if (rx_empty_th.joinable()) {
        rx_empty_th.join();
    }

    assert(rx_empty_stats.bytes_written == 0);
    assert(rx_empty_stats.sha256 == tx_empty_stats.sha256);
    std::cout << "[PASS] Empty file streaming verified.\n";

    std::cout << "UDP Streaming integration tests passed successfully!\n";
    return 0;
}
