#include "benchmark/metrics.hpp"
#include "common/protocol.hpp"
#include "common/logger.hpp"
#include "common/fd.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
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
              << "  -a, --host <ip>         Server target host (default: 127.0.0.1)\n"
              << "  -p, --port <port>       Server target port (default: 8080)\n"
              << "  -c, --concurrency <n>   Concurrent client threads (default: 8)\n"
              << "  -r, --requests <n>      Requests per client thread (default: 100)\n"
              << "  -s, --size <bytes>      Payload size per request (default: 128)\n"
              << "  -o, --output <prefix>   Output report path prefix (generates .json, .csv, .md)\n"
              << "  -v, --verbose           Enable debug logging\n"
              << "  -h, --help              Show this help\n";
}

struct ThreadStats {
    std::vector<double> connect_latencies_us;
    std::vector<double> request_latencies_us;
    size_t successes{0};
    size_t failures{0};
    uint64_t bytes_transferred{0};
};

static void run_client_worker(
    const std::string& host,
    uint16_t port,
    size_t requests,
    size_t payload_size,
    ThreadStats& stats) {

    stats.connect_latencies_us.reserve(1);
    stats.request_latencies_us.reserve(requests);

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        stats.failures += requests;
        return;
    }

    UniqueFd sock(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!sock.valid()) {
        stats.failures += requests;
        return;
    }

    auto t_conn_start = std::chrono::steady_clock::now();
    if (connect(sock.get(), reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        stats.failures += requests;
        return;
    }
    auto t_conn_end = std::chrono::steady_clock::now();
    stats.connect_latencies_us.push_back(
        static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(t_conn_end - t_conn_start).count())
    );

    // Prepare fixed payload with recognizable pattern
    std::vector<uint8_t> payload(payload_size);
    for (size_t i = 0; i < payload_size; ++i) {
        payload[i] = static_cast<uint8_t>((i + 37) & 0xFF);
    }
    std::vector<uint8_t> framed = serialize_frame(payload);

    FrameParser parser;
    uint8_t recv_buf[8192];

    for (size_t req = 0; req < requests; ++req) {
        auto t_req_start = std::chrono::steady_clock::now();

        ssize_t sent = ::send(sock.get(), framed.data(), framed.size(), MSG_NOSIGNAL);
        if (sent != static_cast<ssize_t>(framed.size())) {
            stats.failures++;
            continue;
        }

        Frame resp;
        bool ok = false;
        while (!ok) {
            ssize_t n = ::recv(sock.get(), recv_buf, sizeof(recv_buf), 0);
            if (n <= 0) {
                break;
            }
            parser.append(recv_buf, static_cast<size_t>(n));
            ParseStatus st = parser.next_frame(resp);
            if (st == ParseStatus::Ok) {
                ok = true;
            } else if (st != ParseStatus::NeedMoreData) {
                break;
            }
        }

        auto t_req_end = std::chrono::steady_clock::now();
        if (ok && resp.payload == payload) {
            stats.successes++;
            stats.bytes_transferred += (framed.size() + resp.size() + FRAME_HEADER_SIZE);
            double lat_us = static_cast<double>(
                std::chrono::duration_cast<std::chrono::microseconds>(t_req_end - t_req_start).count()
            );
            stats.request_latencies_us.push_back(lat_us);
        } else {
            stats.failures++;
        }
    }
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    uint16_t port = 8080;
    size_t concurrency = 8;
    size_t requests_per_client = 100;
    size_t payload_size = 128;
    std::string output_prefix;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if ((arg == "-a" || arg == "--host") && i + 1 < argc) {
            host = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if ((arg == "-c" || arg == "--concurrency") && i + 1 < argc) {
            concurrency = static_cast<size_t>(std::atol(argv[++i]));
        } else if ((arg == "-r" || arg == "--requests") && i + 1 < argc) {
            requests_per_client = static_cast<size_t>(std::atol(argv[++i]));
        } else if ((arg == "-s" || arg == "--size") && i + 1 < argc) {
            payload_size = static_cast<size_t>(std::atol(argv[++i]));
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_prefix = argv[++i];
        } else if (arg == "-v" || arg == "--verbose") {
            Logger::instance().set_level(LogLevel::DEBUG);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    LOG_INFO << "Starting TCP Load Generator:";
    LOG_INFO << "Target: " << host << ":" << port
             << ", Concurrency: " << concurrency
             << ", Requests/client: " << requests_per_client
             << ", Payload size: " << payload_size << " bytes"
             << ", Total requests: " << (concurrency * requests_per_client);

    std::vector<ThreadStats> thread_stats(concurrency);
    std::vector<std::thread> threads;
    threads.reserve(concurrency);

    auto t_bench_start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < concurrency; ++i) {
        threads.emplace_back(run_client_worker, host, port, requests_per_client, payload_size, std::ref(thread_stats[i]));
    }

    for (auto& th : threads) {
        if (th.joinable()) {
            th.join();
        }
    }

    auto t_bench_end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(t_bench_end - t_bench_start);

    // Aggregate thread stats into single MetricsCollector
    MetricsCollector collector;
    for (const auto& ts : thread_stats) {
        for (double us : ts.connect_latencies_us) {
            collector.record_connect_time(us);
        }
        for (double us : ts.request_latencies_us) {
            collector.record_request_latency(us);
        }
        for (size_t s = 0; s < ts.successes; ++s) {
            collector.record_success(ts.bytes_transferred / (ts.successes > 0 ? ts.successes : 1));
        }
        for (size_t f = 0; f < ts.failures; ++f) {
            collector.record_failure();
        }
    }

    BenchmarkResult result = collector.aggregate(
        "TCP Echo Server Load Benchmark",
        concurrency,
        requests_per_client,
        payload_size,
        duration
    );

    result.print_summary(std::cout);

    if (!output_prefix.empty()) {
        std::ofstream json_file(output_prefix + ".json");
        if (json_file.is_open()) {
            json_file << result.to_json();
            std::cout << "Report saved: " << output_prefix << ".json\n";
        }

        std::ofstream csv_file(output_prefix + ".csv");
        if (csv_file.is_open()) {
            csv_file << result.to_csv();
            std::cout << "Report saved: " << output_prefix << ".csv\n";
        }

        std::ofstream md_file(output_prefix + ".md");
        if (md_file.is_open()) {
            md_file << result.to_markdown();
            std::cout << "Report saved: " << output_prefix << ".md\n";
        }
    }

    return (result.failed_requests == 0) ? 0 : 1;
}
