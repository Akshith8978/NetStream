#pragma once

#include <vector>
#include <string>
#include <chrono>
#include <iostream>
#include <cstdint>

namespace netstream {

struct LatencyStats {
    double min_us{0.0};
    double max_us{0.0};
    double mean_us{0.0};
    double stddev_us{0.0};
    double p50_us{0.0};
    double p90_us{0.0};
    double p95_us{0.0};
    double p99_us{0.0};
    size_t sample_count{0};
};

struct BenchmarkResult {
    std::string test_name;
    size_t concurrency{1};
    size_t requests_per_client{1};
    size_t payload_size{0};
    size_t total_requests{0};
    size_t successful_requests{0};
    size_t failed_requests{0};
    uint64_t total_bytes_transferred{0};

    std::chrono::microseconds total_duration{0};
    double requests_per_second{0.0};
    double throughput_mbps{0.0};

    LatencyStats connect_latency;
    LatencyStats request_latency;

    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] std::string to_csv() const;
    [[nodiscard]] std::string to_markdown() const;
    void print_summary(std::ostream& os = std::cout) const;
};

class MetricsCollector {
public:
    void record_connect_time(double us);
    void record_request_latency(double us);
    void record_success(size_t bytes);
    void record_failure();

    [[nodiscard]] BenchmarkResult aggregate(
        const std::string& test_name,
        size_t concurrency,
        size_t requests_per_client,
        size_t payload_size,
        std::chrono::microseconds total_duration) const;

private:
    static LatencyStats compute_stats(std::vector<double> samples);

    std::vector<double> connect_times_us_;
    std::vector<double> request_latencies_us_;
    size_t successes_{0};
    size_t failures_{0};
    uint64_t bytes_transferred_{0};
};

} // namespace netstream
