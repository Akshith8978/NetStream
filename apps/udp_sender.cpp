#include "udp/streamer.hpp"
#include "common/logger.hpp"
#include <iostream>
#include <string>
#include <cstdlib>

using namespace netstream;

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " -f <input_file> [options]\n"
              << "Options:\n"
              << "  -f, --file <path>     Path of file to stream (required)\n"
              << "  -a, --host <ip>       Target receiver address (default: 127.0.0.1)\n"
              << "  -p, --port <port>     Target receiver port (default: 9000)\n"
              << "  -c, --chunk <bytes>   Payload chunk size (default: 1400)\n"
              << "  -i, --interval <us>   Pacing interval in microseconds (default: 1000)\n"
              << "  -s, --stream <id>     Stream session ID (default: 1001)\n"
              << "  -v, --verbose         Enable debug logging\n"
              << "  -h, --help            Show this help\n";
}

int main(int argc, char* argv[]) {
    std::string filepath;
    UdpSenderConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if ((arg == "-f" || arg == "--file") && i + 1 < argc) {
            filepath = argv[++i];
        } else if ((arg == "-a" || arg == "--host") && i + 1 < argc) {
            config.target_host = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            config.target_port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if ((arg == "-c" || arg == "--chunk") && i + 1 < argc) {
            config.chunk_size = static_cast<size_t>(std::atol(argv[++i]));
        } else if ((arg == "-i" || arg == "--interval") && i + 1 < argc) {
            config.pacing_interval = std::chrono::microseconds(std::atol(argv[++i]));
        } else if ((arg == "-s" || arg == "--stream") && i + 1 < argc) {
            config.stream_id = static_cast<uint32_t>(std::atol(argv[++i]));
        } else if (arg == "-v" || arg == "--verbose") {
            Logger::instance().set_level(LogLevel::DEBUG);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (filepath.empty()) {
        std::cerr << "Error: Input file must be specified with -f\n";
        print_usage(argv[0]);
        return 1;
    }

    UdpSender sender(config);
    auto stats = sender.send_file(filepath);

    std::cout << "\n--- UdpSender Summary ---\n"
              << "File Size:   " << stats.file_size << " bytes\n"
              << "SHA-256:     " << stats.sha256 << "\n"
              << "Packets Sent:" << stats.packets_sent << "\n"
              << "Bytes Sent:  " << stats.bytes_sent << "\n"
              << "Throughput:  " << stats.throughput_mbps << " Mbps\n";

    return (stats.packets_sent > 0 || stats.file_size == 0) ? 0 : 1;
}
