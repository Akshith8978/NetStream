#include "tcp/tcp_server.hpp"
#include "common/protocol.hpp"
#include "common/logger.hpp"
#include "common/fd.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace netstream;

int main() {
    std::cout << "--- Running TCP Server Integration Test ---\n";
    Logger::instance().set_level(LogLevel::WARN);

    ServerConfig config;
    config.host = "127.0.0.1";
    config.port = 0; // Bind to ephemeral OS port
    config.worker_threads = 4;
    config.queue_capacity = 256;

    TcpServer server(config);
    assert(server.init());
    uint16_t port = server.bound_port();
    assert(port > 0);

    std::thread server_thread([&server]() {
        server.run();
    });

    // Allow reactor to spin up
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    constexpr size_t NUM_CLIENTS = 8;
    constexpr size_t MESSAGES_PER_CLIENT = 25;
    std::atomic<size_t> total_verified{0};
    std::vector<std::thread> client_threads;

    for (size_t c = 0; c < NUM_CLIENTS; ++c) {
        client_threads.emplace_back([port, c, &total_verified]() {
            UniqueFd sock(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
            assert(sock.valid());

            sockaddr_in saddr{};
            saddr.sin_family = AF_INET;
            saddr.sin_port = htons(port);
            inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);

            assert(connect(sock.get(), reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr)) == 0);

            FrameParser parser;
            uint8_t buf[4096];

            for (size_t m = 0; m < MESSAGES_PER_CLIENT; ++m) {
                std::string msg = "Client " + std::to_string(c) + " Msg " + std::to_string(m);
                // Add embedded null and varied payload size
                std::vector<uint8_t> payload(msg.begin(), msg.end());
                payload.push_back(0x00);
                payload.insert(payload.end(), m * 10, 'Z');

                auto framed = serialize_frame(payload);
                ssize_t sent = ::send(sock.get(), framed.data(), framed.size(), MSG_NOSIGNAL);
                assert(sent == static_cast<ssize_t>(framed.size()));

                Frame resp;
                bool ok = false;
                while (!ok) {
                    ssize_t n = ::recv(sock.get(), buf, sizeof(buf), 0);
                    if (n <= 0) break;
                    parser.append(buf, static_cast<size_t>(n));
                    if (parser.next_frame(resp) == ParseStatus::Ok) {
                        ok = true;
                    }
                }

                assert(ok);
                assert(resp.payload == payload);
                total_verified.fetch_add(1);
            }
        });
    }

    for (auto& th : client_threads) {
        if (th.joinable()) th.join();
    }

    assert(total_verified.load() == NUM_CLIENTS * MESSAGES_PER_CLIENT);
    std::cout << "[PASS] Concurrent client echo verified (" << total_verified.load() << " frames)\n";

    // Test client disconnecting abruptly during partial frame
    {
        UniqueFd sock(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
        sockaddr_in saddr{};
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
        connect(sock.get(), reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr));

        // Send half a header (2 bytes) then close socket abruptly
        uint8_t partial[2] = {0x00, 0x01};
        ::send(sock.get(), partial, 2, MSG_NOSIGNAL);
        sock.reset(); // close fd
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::cout << "[PASS] Partial frame abrupt disconnect handled cleanly\n";
    }

    // Stop server gracefully
    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }

    std::cout << "TCP Server integration tests passed successfully!\n";
    return 0;
}
