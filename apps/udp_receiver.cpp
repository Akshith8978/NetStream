#include "udp/streamer.hpp"
#include "common/logger.hpp"
#include <iostream>
#include <string>
#include <cstdlib>

using namespace netstream;

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " -o <output_file> [options]\n"
              << "Options:\n"
              << "  -o, --output <path>   Destination path for received file (required)\n"
              << "  -a, --host <ip>       Bind host address (default: 0.0.0.0)\n"
              << "  -p, --port <port>     Bind port (default: 9000)\n"
              << "  -s, --stream <id>     Filter by stream session ID (default: 0 = any)\n"
              << "  -d, --delay <ms>      Jitter buffer initial playout delay (default: 50 ms)\n"
              << "  -t, --timeout <ms>    Jitter buffer packet loss deadline (default: 100 ms)\n"
              << "  -c, --cap <count>     Jitter buffer capacity (default: 512)\n"
              << "  -v, --verbose         Enable debug logging\n"
              << "  -h, --help            Show this help\n";
}

int main(int argc, char* argv[]) {
    std::string output_path;
    UdpReceiverConfig config;
    uint32_t stream_id = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_path = argv[++i];
        } else if ((arg == "-a" || arg == "--host") && i + 1 < argc) {
            config.bind_host = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            config.bind_port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if ((arg == "-s" || arg == "--stream") && i + 1 < argc) {
            stream_id = static_cast<uint32_t>(std::atol(argv[++i]));
        } else if ((arg == "-d" || arg == "--delay") && i + 1 < argc) {
            config.jitter_config.playout_delay = std::chrono::milliseconds(std::atol(argv[++i]));
        } else if ((arg == "-t" || arg == "--timeout") && i + 1 < argc) {
            config.jitter_config.packet_timeout = std::chrono::milliseconds(std::atol(argv[++i]));
        } else if ((arg == "-c" || arg == "--cap") && i + 1 < argc) {
            config.jitter_config.capacity = static_cast<size_t>(std::atol(argv[++i]));
        } else if (arg == "-v" || arg == "--verbose") {
            Logger::instance().set_level(LogLevel::DEBUG);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (output_path.empty()) {
        std::cerr << "Error: Output file must be specified with -o\n";
        print_usage(argv[0]);
        return 1;
    }

    UdpReceiver receiver(config);
    auto stats = receiver.receive_file(output_path, stream_id);

    std::cout << "\n--- UdpReceiver Summary ---\n"
              << "Output File:       " << output_path << "\n"
              << "Bytes Written:     " << stats.bytes_written << "\n"
              << "Expected Bytes:    " << stats.total_expected_bytes << "\n"
              << "Packets Received:  " << stats.packets_received << "\n"
              << "Packets Delivered: " << stats.packets_delivered << "\n"
              << "Packets Lost:      " << stats.packets_lost << "\n"
              << "Packets Late:      " << stats.packets_late << "\n"
              << "Duplicates:        " << stats.packets_duplicate << "\n"
              << "Reordered:         " << stats.packets_reordered << "\n"
              << "Goodput:           " << stats.goodput_mbps << " Mbps\n"
              << "File Complete:     " << (stats.is_complete ? "YES" : "NO") << "\n"
              << "SHA-256:           " << stats.sha256 << "\n";

    return stats.is_complete ? 0 : 2;
}
