#include "benchmark/metrics.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace netstream {

void MetricsCollector::record_connect_time(double us) {
    connect_times_us_.push_back(us);
}

void MetricsCollector::record_request_latency(double us) {
    request_latencies_us_.push_back(us);
}

void MetricsCollector::record_success(size_t bytes) {
    successes_++;
    bytes_transferred_ += bytes;
}

void MetricsCollector::record_failure() {
    failures_++;
}

LatencyStats MetricsCollector::compute_stats(std::vector<double> samples) {
    LatencyStats stats;
    if (samples.empty()) {
        return stats;
    }

    stats.sample_count = samples.size();
    std::sort(samples.begin(), samples.end());

    stats.min_us = samples.front();
    stats.max_us = samples.back();

    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    stats.mean_us = sum / static_cast<double>(samples.size());

    double sq_diff_sum = 0.0;
    for (double val : samples) {
        sq_diff_sum += (val - stats.mean_us) * (val - stats.mean_us);
    }
    stats.stddev_us = std::sqrt(sq_diff_sum / static_cast<double>(samples.size()));

    auto percentile = [&samples](double p) -> double {
        size_t idx = static_cast<size_t>(std::ceil(p * static_cast<double>(samples.size()))) - 1;
        if (idx >= samples.size()) idx = samples.size() - 1;
        return samples[idx];
    };

    stats.p50_us = percentile(0.50);
    stats.p90_us = percentile(0.90);
    stats.p95_us = percentile(0.95);
    stats.p99_us = percentile(0.99);

    return stats;
}

BenchmarkResult MetricsCollector::aggregate(
    const std::string& test_name,
    size_t concurrency,
    size_t requests_per_client,
    size_t payload_size,
    std::chrono::microseconds total_duration) const {

    BenchmarkResult res;
    res.test_name = test_name;
    res.concurrency = concurrency;
    res.requests_per_client = requests_per_client;
    res.payload_size = payload_size;
    res.total_requests = successes_ + failures_;
    res.successful_requests = successes_;
    res.failed_requests = failures_;
    res.total_bytes_transferred = bytes_transferred_;
    res.total_duration = total_duration;

    double total_sec = std::max(0.000001, static_cast<double>(total_duration.count()) / 1e6);
    res.requests_per_second = static_cast<double>(res.successful_requests) / total_sec;
    res.throughput_mbps = (static_cast<double>(res.total_bytes_transferred) * 8.0) / (total_sec * 1e6);

    res.connect_latency = compute_stats(connect_times_us_);
    res.request_latency = compute_stats(request_latencies_us_);

    return res;
}

std::string BenchmarkResult::to_json() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "{\n"
       << "  \"test_name\": \"" << test_name << "\",\n"
       << "  \"concurrency\": " << concurrency << ",\n"
       << "  \"requests_per_client\": " << requests_per_client << ",\n"
       << "  \"payload_size_bytes\": " << payload_size << ",\n"
       << "  \"total_requests\": " << total_requests << ",\n"
       << "  \"successful_requests\": " << successful_requests << ",\n"
       << "  \"failed_requests\": " << failed_requests << ",\n"
       << "  \"bytes_transferred\": " << total_bytes_transferred << ",\n"
       << "  \"duration_ms\": " << (static_cast<double>(total_duration.count()) / 1000.0) << ",\n"
       << "  \"requests_per_second\": " << requests_per_second << ",\n"
       << "  \"throughput_mbps\": " << throughput_mbps << ",\n"
       << "  \"request_latency_us\": {\n"
       << "    \"min\": " << request_latency.min_us << ",\n"
       << "    \"mean\": " << request_latency.mean_us << ",\n"
       << "    \"p50\": " << request_latency.p50_us << ",\n"
       << "    \"p90\": " << request_latency.p90_us << ",\n"
       << "    \"p95\": " << request_latency.p95_us << ",\n"
       << "    \"p99\": " << request_latency.p99_us << ",\n"
       << "    \"max\": " << request_latency.max_us << "\n"
       << "  }\n"
       << "}\n";
    return ss.str();
}

std::string BenchmarkResult::to_csv() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "test_name,concurrency,requests_per_client,payload_bytes,total_reqs,success_reqs,failed_reqs,duration_ms,rps,mbps,lat_p50_us,lat_p95_us,lat_p99_us,lat_max_us\n"
       << test_name << ","
       << concurrency << ","
       << requests_per_client << ","
       << payload_size << ","
       << total_requests << ","
       << successful_requests << ","
       << failed_requests << ","
       << (static_cast<double>(total_duration.count()) / 1000.0) << ","
       << requests_per_second << ","
       << throughput_mbps << ","
       << request_latency.p50_us << ","
       << request_latency.p95_us << ","
       << request_latency.p99_us << ","
       << request_latency.max_us << "\n";
    return ss.str();
}

std::string BenchmarkResult::to_markdown() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "### Benchmark Result: " << test_name << "\n\n"
       << "| Metric | Value |\n"
       << "| :--- | :--- |\n"
       << "| Concurrency | " << concurrency << " clients |\n"
       << "| Requests / Client | " << requests_per_client << " |\n"
       << "| Payload Size | " << payload_size << " bytes |\n"
       << "| Successful Requests | " << successful_requests << " / " << total_requests << " |\n"
       << "| Elapsed Duration | " << (static_cast<double>(total_duration.count()) / 1000.0) << " ms |\n"
       << "| **Throughput** | **" << requests_per_second << " req/s** (" << throughput_mbps << " Mbps) |\n"
       << "| Latency (p50) | " << (request_latency.p50_us / 1000.0) << " ms |\n"
       << "| Latency (p90) | " << (request_latency.p90_us / 1000.0) << " ms |\n"
       << "| Latency (p95) | " << (request_latency.p95_us / 1000.0) << " ms |\n"
       << "| Latency (p99) | " << (request_latency.p99_us / 1000.0) << " ms |\n"
       << "| Latency (max) | " << (request_latency.max_us / 1000.0) << " ms |\n\n";
    return ss.str();
}

void BenchmarkResult::print_summary(std::ostream& os) const {
    os << std::fixed << std::setprecision(2)
       << "=========================================================\n"
       << " Benchmark: " << test_name << "\n"
       << "=========================================================\n"
       << " Concurrency:         " << concurrency << "\n"
       << " Requests per client: " << requests_per_client << "\n"
       << " Payload size:        " << payload_size << " bytes\n"
       << " Total requests:      " << total_requests << " (Success: " << successful_requests << ", Fail: " << failed_requests << ")\n"
       << " Elapsed duration:    " << (static_cast<double>(total_duration.count()) / 1000.0) << " ms\n"
       << " Throughput:          " << requests_per_second << " req/s (" << throughput_mbps << " Mbps)\n"
       << "---------------------------------------------------------\n"
       << " Request Latency (ms):\n"
       << "   p50: " << (request_latency.p50_us / 1000.0) << " ms\n"
       << "   p90: " << (request_latency.p90_us / 1000.0) << " ms\n"
       << "   p95: " << (request_latency.p95_us / 1000.0) << " ms\n"
       << "   p99: " << (request_latency.p99_us / 1000.0) << " ms\n"
       << "   max: " << (request_latency.max_us / 1000.0) << " ms\n"
       << "=========================================================\n";
}

} // namespace netstream
