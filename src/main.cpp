#include "tcp/tcp_server.hpp"
#include "common/logger.hpp"
#include <iostream>
#include <string>
#include <cstdlib>

using namespace netstream;

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  -a, --host <ip>        Listening address (default: 0.0.0.0)\n"
              << "  -p, --port <port>      Listening port (default: 8080)\n"
              << "  -t, --threads <count>  Worker thread pool size (default: 4)\n"
              << "  -q, --queue <cap>      Bounded queue capacity (default: 1024)\n"
              << "  -m, --max-msg <bytes>  Maximum permitted message size (default: 16MB)\n"
              << "  -v, --verbose          Enable debug logging\n"
              << "  -h, --help             Display this help message\n";
}

int main(int argc, char* argv[]) {
    ServerConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if ((arg == "-a" || arg == "--host") && i + 1 < argc) {
            config.host = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            config.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if ((arg == "-t" || arg == "--threads") && i + 1 < argc) {
            config.worker_threads = static_cast<size_t>(std::atol(argv[++i]));
        } else if ((arg == "-q" || arg == "--queue") && i + 1 < argc) {
            config.queue_capacity = static_cast<size_t>(std::atol(argv[++i]));
        } else if ((arg == "-m" || arg == "--max-msg") && i + 1 < argc) {
            config.max_message_size = static_cast<uint32_t>(std::atol(argv[++i]));
        } else if (arg == "-v" || arg == "--verbose") {
            Logger::instance().set_level(LogLevel::DEBUG);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    LOG_INFO << "Starting NetStream TCP Server on " << config.host << ":" << config.port;
    LOG_INFO << "Worker threads: " << config.worker_threads 
             << ", Queue capacity: " << config.queue_capacity
             << ", Max message size: " << config.max_message_size << " bytes";

    TcpServer server(config);
    if (!server.init()) {
        LOG_FATAL << "Failed to initialize server";
        return 1;
    }

    server.run();
    return 0;
}
