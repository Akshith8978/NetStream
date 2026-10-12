#include "common/logger.hpp"
#include <ctime>

namespace netstream {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::set_level(LogLevel level) noexcept {
    std::lock_guard<std::mutex> lock(log_mutex_);
    current_level_ = level;
}

LogLevel Logger::get_level() const noexcept {
    return current_level_;
}

const char* log_level_to_string(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default:              return "UNKNOWN";
    }
}

void Logger::log(LogLevel level, const char* file, int line, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count() % 1000000;

    std::tm tm_buf{};
    gmtime_r(&now_time_t, &tm_buf);

    // Strip path from file name for cleaner logs
    const char* base_file = file;
    for (const char* p = file; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            base_file = p + 1;
        }
    }

    std::lock_guard<std::mutex> lock(log_mutex_);
    char time_str[32];
    std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

    std::cerr << "[" << time_str << "." 
              << std::setfill('0') << std::setw(6) << now_us << "] "
              << "[" << log_level_to_string(level) << "] "
              << "[" << base_file << ":" << line << "] "
              << message << "\n";
}

} // namespace netstream
