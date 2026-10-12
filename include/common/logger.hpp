#pragma once

#include <string>
#include <sstream>
#include <iostream>
#include <mutex>
#include <chrono>
#include <iomanip>

namespace netstream {

enum class LogLevel {
    DEBUG = 0,
    INFO,
    WARN,
    ERROR,
    FATAL
};

class Logger {
public:
    static Logger& instance();

    void set_level(LogLevel level) noexcept;
    [[nodiscard]] LogLevel get_level() const noexcept;

    void log(LogLevel level, const char* file, int line, const std::string& message);

private:
    Logger() = default;
    LogLevel current_level_{LogLevel::INFO};
    std::mutex log_mutex_;
};

const char* log_level_to_string(LogLevel level) noexcept;

class LogMessage {
public:
    LogMessage(LogLevel level, const char* file, int line)
        : level_(level), file_(file), line_(line) {}

    ~LogMessage() {
        Logger::instance().log(level_, file_, line_, stream_.str());
    }

    template <typename T>
    LogMessage& operator<<(const T& val) {
        stream_ << val;
        return *this;
    }

private:
    LogLevel level_;
    const char* file_;
    int line_;
    std::ostringstream stream_;
};

#define LOG_DEBUG if (netstream::Logger::instance().get_level() <= netstream::LogLevel::DEBUG) \
    netstream::LogMessage(netstream::LogLevel::DEBUG, __FILE__, __LINE__)

#define LOG_INFO if (netstream::Logger::instance().get_level() <= netstream::LogLevel::INFO) \
    netstream::LogMessage(netstream::LogLevel::INFO, __FILE__, __LINE__)

#define LOG_WARN if (netstream::Logger::instance().get_level() <= netstream::LogLevel::WARN) \
    netstream::LogMessage(netstream::LogLevel::WARN, __FILE__, __LINE__)

#define LOG_ERROR if (netstream::Logger::instance().get_level() <= netstream::LogLevel::ERROR) \
    netstream::LogMessage(netstream::LogLevel::ERROR, __FILE__, __LINE__)

#define LOG_FATAL netstream::LogMessage(netstream::LogLevel::FATAL, __FILE__, __LINE__)

} // namespace netstream
