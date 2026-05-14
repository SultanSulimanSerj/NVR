#pragma once

#include <spdlog/spdlog.h>

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

namespace nvr {

enum class LogLevel : int {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
};

class Logger {
public:
    static Logger& instance();

    void initialize(const std::string& file_path = {},
                    size_t              max_size_mb = 64,
                    size_t              files = 5);
    void setLevel(LogLevel lvl);
    LogLevel level() const noexcept { return level_; }

    std::shared_ptr<spdlog::logger> get() const noexcept { return logger_; }

private:
    Logger() = default;
    LogLevel                        level_{LogLevel::Info};
    std::shared_ptr<spdlog::logger> logger_;
};

void installFfmpegLogBridge();

namespace detail {
inline std::string vformat_printf(const char* fmt, ...) {
    char    stack_buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int needed = std::vsnprintf(stack_buf, sizeof(stack_buf), fmt, ap);
    va_end(ap);
    if (needed < 0) return {};
    if (static_cast<size_t>(needed) < sizeof(stack_buf)) {
        return std::string(stack_buf, static_cast<size_t>(needed));
    }
    std::string out(static_cast<size_t>(needed) + 1, '\0');
    va_start(ap, fmt);
    std::vsnprintf(out.data(), out.size(), fmt, ap);
    va_end(ap);
    out.resize(static_cast<size_t>(needed));
    return out;
}
}

#define NVR_LOG_PRINTF(level, tag, ...)                                              \
    do {                                                                             \
        auto _lg = ::nvr::Logger::instance().get();                                  \
        if (_lg && _lg->should_log(level)) {                                         \
            _lg->log(level, "[{}] {}", (tag),                                        \
                     ::nvr::detail::vformat_printf(__VA_ARGS__));                    \
        }                                                                            \
    } while (0)

#define NVR_TRACE(tag, ...) NVR_LOG_PRINTF(spdlog::level::trace, tag, __VA_ARGS__)
#define NVR_DEBUG(tag, ...) NVR_LOG_PRINTF(spdlog::level::debug, tag, __VA_ARGS__)
#define NVR_INFO(tag, ...)  NVR_LOG_PRINTF(spdlog::level::info,  tag, __VA_ARGS__)
#define NVR_WARN(tag, ...)  NVR_LOG_PRINTF(spdlog::level::warn,  tag, __VA_ARGS__)
#define NVR_ERROR(tag, ...) NVR_LOG_PRINTF(spdlog::level::err,   tag, __VA_ARGS__)

}
