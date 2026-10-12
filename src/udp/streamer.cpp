#include "udp/streamer.hpp"
#include "common/logger.hpp"
#include "common/sha256.hpp"
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>
#include <thread>
#include <cstring>
#include <cerrno>

namespace netstream {

UdpSender::UdpSender(UdpSenderConfig config)
    : config_(std::move(config)) {}

UdpSenderStats UdpSender::send_file(const std::string& filepath) {
    UdpSenderStats stats;
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR << "UdpSender: Unable to open input file: " << filepath;
        return stats;
    }

    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    stats.file_size = static_cast<uint64_t>(file_size);
    stats.sha256 = Sha256::hash_file(filepath);

    UniqueFd sock(socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
    if (!sock.valid()) {
        LOG_ERROR << "UdpSender: Failed to create socket: " << strerror(errno);
        return stats;
    }

    sockaddr_in dest_addr{};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(config_.target_port);
    if (inet_pton(AF_INET, config_.target_host.c_str(), &dest_addr.sin_addr) <= 0) {
        LOG_ERROR << "UdpSender: Invalid destination host: " << config_.target_host;
        return stats;
    }

    LOG_INFO << "UdpSender: Streaming file " << filepath << " (" << stats.file_size 
             << " bytes) to " << config_.target_host << ":" << config_.target_port
             << " [SHA256: " << stats.sha256 << "]";

    auto start_time = std::chrono::steady_clock::now();
    uint32_t current_seq = config_.initial_seq;
    uint64_t current_offset = 0;
    std::vector<uint8_t> chunk_buf(config_.chunk_size);

    bool first_chunk = true;

    // Handle empty file edge case
    if (file_size == 0) {
        UdpPacket pkt;
        pkt.header.version = PROTOCOL_VERSION;
        pkt.header.payload_type = PAYLOAD_TYPE_MEDIA;
        pkt.header.flags = FLAG_START_OF_STREAM | FLAG_END_OF_STREAM;
        pkt.header.stream_id = config_.stream_id;
        pkt.header.sequence_number = current_seq++;
        pkt.header.timestamp = 0;
        pkt.header.payload_length = 0;
        pkt.header.file_offset = 0;

        auto serialized = pkt.serialize();
        ::sendto(sock.get(), serialized.data(), serialized.size(), 0,
                 reinterpret_cast<sockaddr*>(&dest_addr), sizeof(dest_addr));
        stats.packets_sent = 1;
        stats.bytes_sent = 0;
    }

    while (current_offset < static_cast<uint64_t>(file_size)) {
        size_t to_read = static_cast<size_t>(std::min(
            static_cast<uint64_t>(config_.chunk_size),
            static_cast<uint64_t>(file_size) - current_offset
        ));

        file.read(reinterpret_cast<char*>(chunk_buf.data()), static_cast<std::streamsize>(to_read));
        size_t bytes_read = static_cast<size_t>(file.gcount());
        if (bytes_read == 0) break;

        UdpPacket pkt;
        pkt.header.version = PROTOCOL_VERSION;
        pkt.header.payload_type = PAYLOAD_TYPE_MEDIA;
        pkt.header.flags = 0;
        if (first_chunk) {
            pkt.header.flags |= FLAG_START_OF_STREAM;
            first_chunk = false;
        }

        bool is_last = (current_offset + bytes_read >= static_cast<uint64_t>(file_size));
        if (is_last) {
            pkt.header.flags |= FLAG_END_OF_STREAM;
        }

        pkt.header.stream_id = config_.stream_id;
        pkt.header.sequence_number = current_seq++;

        auto now = std::chrono::steady_clock::now();
        pkt.header.timestamp = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count()
        );
        pkt.header.payload_length = static_cast<uint32_t>(bytes_read);
        pkt.header.file_offset = current_offset;
        pkt.payload.assign(chunk_buf.begin(), chunk_buf.begin() + static_cast<ptrdiff_t>(bytes_read));

        auto serialized = pkt.serialize();
        ssize_t sent = ::sendto(sock.get(), serialized.data(), serialized.size(), 0,
                                reinterpret_cast<sockaddr*>(&dest_addr), sizeof(dest_addr));
        if (sent > 0) {
            stats.packets_sent++;
            stats.bytes_sent += bytes_read;
        }

        current_offset += bytes_read;

        if (config_.pacing_interval.count() > 0) {
            std::this_thread::sleep_for(config_.pacing_interval);
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    stats.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    double sec = std::max(0.000001, static_cast<double>(stats.duration.count()) / 1e6);
    stats.throughput_mbps = (static_cast<double>(stats.bytes_sent) * 8.0) / (sec * 1e6);

    LOG_INFO << "UdpSender: Finished sending. Packets: " << stats.packets_sent 
             << ", Bytes: " << stats.bytes_sent
             << ", Duration: " << (sec * 1000.0) << " ms, Throughput: " 
             << stats.throughput_mbps << " Mbps";

    return stats;
}

UdpReceiver::UdpReceiver(UdpReceiverConfig config)
    : config_(std::move(config)) {}

UdpReceiverStats UdpReceiver::receive_file(const std::string& output_filepath, uint32_t expected_stream_id) {
    UdpReceiverStats stats;
    UniqueFd sock(socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
    if (!sock.valid()) {
        LOG_ERROR << "UdpReceiver: Failed to create socket: " << strerror(errno);
        return stats;
    }

    int opt = 1;
    setsockopt(sock.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Configure 100ms receive timeout so we can periodically check for playout and termination
    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 100000; // 100 ms
    setsockopt(sock.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(config_.bind_port);
    if (inet_pton(AF_INET, config_.bind_host.c_str(), &bind_addr.sin_addr) <= 0) {
        LOG_ERROR << "UdpReceiver: Invalid bind address: " << config_.bind_host;
        return stats;
    }

    if (bind(sock.get(), reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
        LOG_ERROR << "UdpReceiver: Bind failed on " << config_.bind_host << ":" << config_.bind_port << ": " << strerror(errno);
        return stats;
    }

    std::ofstream out(output_filepath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        LOG_ERROR << "UdpReceiver: Unable to open output file: " << output_filepath;
        return stats;
    }

    LOG_INFO << "UdpReceiver: Listening on " << config_.bind_host << ":" << config_.bind_port 
             << ", writing to " << output_filepath;

    JitterBuffer jitter(config_.jitter_config);
    std::vector<uint8_t> recv_buf(65536);

    auto start_time = std::chrono::steady_clock::now();
    auto last_packet_time = start_time;
    bool eos_received = false;
    bool stream_started = false;

    while (true) {
        auto now = std::chrono::steady_clock::now();
        ssize_t n = ::recvfrom(sock.get(), recv_buf.data(), recv_buf.size(), 0, nullptr, nullptr);
        if (n > 0) {
            auto opt_pkt = UdpPacket::deserialize(recv_buf.data(), static_cast<size_t>(n));
            if (opt_pkt.has_value()) {
                auto& pkt = opt_pkt.value();
                if (expected_stream_id == 0 || pkt.header.stream_id == expected_stream_id) {
                    if (!stream_started) {
                        stream_started = true;
                        start_time = now;
                    }
                    last_packet_time = now;
                    if (pkt.header.is_end_of_stream()) {
                        eos_received = true;
                        stats.total_expected_bytes = pkt.header.file_offset + pkt.header.payload_length;
                    }
                    jitter.push(std::move(pkt), now);
                }
            }
        } else {
            // Check for timeout / stream completion
            if (stream_started) {
                if (eos_received && jitter.empty()) {
                    break;
                }
                if (now - last_packet_time >= config_.idle_timeout) {
                    LOG_WARN << "UdpReceiver: Idle timeout reached, closing stream";
                    break;
                }
            }
        }

        // Drain in-order packets ready for playout
        while (auto ready_pkt = jitter.pop(now)) {
            if (!ready_pkt->payload.empty()) {
                out.seekp(static_cast<std::streamoff>(ready_pkt->header.file_offset));
                out.write(reinterpret_cast<const char*>(ready_pkt->payload.data()), 
                          static_cast<std::streamsize>(ready_pkt->payload.size()));
                stats.bytes_written += ready_pkt->payload.size();
            }
        }

        // If EOS received and playout is done, break
        if (eos_received && jitter.empty()) {
            break;
        }
    }

    // Drain any remaining buffered packets in jitter buffer
    while (auto ready_pkt = jitter.pop_immediate()) {
        if (!ready_pkt->payload.empty()) {
            out.seekp(static_cast<std::streamoff>(ready_pkt->header.file_offset));
            out.write(reinterpret_cast<const char*>(ready_pkt->payload.data()), 
                      static_cast<std::streamsize>(ready_pkt->payload.size()));
            stats.bytes_written += ready_pkt->payload.size();
        }
    }

    out.flush();
    out.close();

    auto end_time = std::chrono::steady_clock::now();
    stats.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    double sec = std::max(0.000001, static_cast<double>(stats.duration.count()) / 1e6);
    stats.goodput_mbps = (static_cast<double>(stats.bytes_written) * 8.0) / (sec * 1e6);

    auto jm = jitter.metrics();
    stats.packets_received = jm.packets_received;
    stats.packets_delivered = jm.packets_delivered;
    stats.packets_lost = jm.packets_lost;
    stats.packets_duplicate = jm.packets_duplicate;
    stats.packets_late = jm.packets_late;
    stats.packets_reordered = jm.packets_reordered;

    stats.sha256 = Sha256::hash_file(output_filepath);
    stats.is_complete = (stats.packets_lost == 0) && (stats.bytes_written == stats.total_expected_bytes);

    LOG_INFO << "UdpReceiver: Finished reception. Received: " << stats.packets_received
             << ", Delivered: " << stats.packets_delivered
             << ", Lost: " << stats.packets_lost
             << ", Bytes written: " << stats.bytes_written
             << " / Expected: " << stats.total_expected_bytes
             << ", Complete: " << (stats.is_complete ? "YES" : "NO")
             << ", SHA256: " << stats.sha256;

    return stats;
}

} // namespace netstream
