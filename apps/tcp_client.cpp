#include "common/protocol.hpp"
#include "common/logger.hpp"
#include "common/fd.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace netstream;

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  -a, --host <ip>       Server host address (default: 127.0.0.1)\n"
              << "  -p, --port <port>     Server port (default: 8080)\n"
              << "  -c, --count <n>       Number of messages to send (default: 5)\n"
              << "  -s, --size <bytes>    Generated payload size in bytes (default: 64)\n"
              << "  -m, --message <str>   Custom text message to send\n"
              << "  -b, --binary          Include embedded nulls and binary bytes\n"
              << "  -v, --verbose         Enable debug logging\n"
              << "  -h, --help            Show this help\n";
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    uint16_t port = 8080;
    size_t count = 5;
    size_t size = 64;
    std::string custom_message;
    bool binary_mode = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if ((arg == "-a" || arg == "--host") && i + 1 < argc) {
            host = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if ((arg == "-c" || arg == "--count") && i + 1 < argc) {
            count = static_cast<size_t>(std::atol(argv[++i]));
        } else if ((arg == "-s" || arg == "--size") && i + 1 < argc) {
            size = static_cast<size_t>(std::atol(argv[++i]));
        } else if ((arg == "-m" || arg == "--message") && i + 1 < argc) {
            custom_message = argv[++i];
        } else if (arg == "-b" || arg == "--binary") {
            binary_mode = true;
        } else if (arg == "-v" || arg == "--verbose") {
            Logger::instance().set_level(LogLevel::DEBUG);
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    UniqueFd sock(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!sock.valid()) {
        LOG_FATAL << "Failed to create socket: " << strerror(errno);
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        LOG_FATAL << "Invalid host address: " << host;
        return 1;
    }

    LOG_INFO << "Connecting to " << host << ":" << port << "...";
    if (connect(sock.get(), reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        LOG_FATAL << "Connect failed: " << strerror(errno);
        return 1;
    }
    LOG_INFO << "Connected successfully.";

    FrameParser parser;
    uint8_t recv_buf[8192];
    size_t verified = 0;

    for (size_t i = 0; i < count; ++i) {
        std::vector<uint8_t> payload;
        if (!custom_message.empty()) {
            std::string msg = custom_message + " #" + std::to_string(i + 1);
            payload.assign(msg.begin(), msg.end());
        } else if (binary_mode) {
            payload.resize(size);
            for (size_t j = 0; j < size; ++j) {
                // Include null bytes and high bytes
                payload[j] = static_cast<uint8_t>((j % 2 == 0) ? 0x00 : (j & 0xFF));
            }
        } else {
            std::string text = "NetStream TCP Frame #" + std::to_string(i + 1);
            payload.assign(text.begin(), text.end());
            if (payload.size() < size) {
                payload.resize(size, 'X');
            }
        }

        std::vector<uint8_t> frame = serialize_frame(payload);
        auto t_start = std::chrono::steady_clock::now();

        ssize_t sent = ::send(sock.get(), frame.data(), frame.size(), MSG_NOSIGNAL);
        if (sent != static_cast<ssize_t>(frame.size())) {
            LOG_ERROR << "Failed to send full frame: " << strerror(errno);
            break;
        }

        // Read echo response
        Frame response;
        bool got_frame = false;
        while (!got_frame) {
            ssize_t n = ::recv(sock.get(), recv_buf, sizeof(recv_buf), 0);
            if (n <= 0) {
                LOG_ERROR << "Server disconnected or error: " << strerror(errno);
                break;
            }
            parser.append(recv_buf, static_cast<size_t>(n));
            ParseStatus status = parser.next_frame(response);
            if (status == ParseStatus::Ok) {
                got_frame = true;
            } else if (status != ParseStatus::NeedMoreData) {
                LOG_ERROR << "Parse error: " << static_cast<int>(status);
                break;
            }
        }

        if (got_frame) {
            auto t_end = std::chrono::steady_clock::now();
            auto latency_us = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();

            if (response.payload == payload) {
                verified++;
                LOG_INFO << "Message " << (i + 1) << "/" << count 
                         << " (" << payload.size() << " bytes) echoed successfully in " 
                         << (latency_us / 1000.0) << " ms [VALIDATED]";
            } else {
                LOG_ERROR << "Payload mismatch for message " << (i + 1);
            }
        }
    }

    LOG_INFO << "Finished. Verified: " << verified << "/" << count << " messages.";
    return (verified == count) ? 0 : 1;
}
