#include "nvr/Logger.hpp"

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <filesystem>
#include <mutex>
#include <vector>

extern "C" {
#include <libavutil/log.h>
}

namespace nvr {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

namespace {
std::once_flag g_async_init_flag;
void ensureAsyncInitialized() {
    std::call_once(g_async_init_flag, [] {
        spdlog::init_thread_pool(8192, 1);
    });
}
}

void Logger::initialize(const std::string& file_path, size_t max_size_mb, size_t files) {
    ensureAsyncInitialized();

    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    if (!file_path.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(
            std::filesystem::path(file_path).parent_path(), ec);
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            file_path, max_size_mb * 1024 * 1024, files));
    }

    logger_ = std::make_shared<spdlog::async_logger>(
        "nvr", sinks.begin(), sinks.end(),
        spdlog::thread_pool(), spdlog::async_overflow_policy::overrun_oldest);
    const char* fmt = std::getenv("NVR_LOG_FORMAT");
    if (fmt && std::string(fmt) == "json") {
        logger_->set_pattern(
            R"({"ts":"%Y-%m-%dT%H:%M:%S.%f%z","level":"%l","logger":"nvr","msg":%v})");
    } else {
        logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%^%l%$] %v");
    }
    logger_->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(logger_);
}

void Logger::setLevel(LogLevel lvl) {
    level_ = lvl;
    if (!logger_) initialize();
    spdlog::level::level_enum sl = spdlog::level::info;
    switch (lvl) {
        case LogLevel::Trace: sl = spdlog::level::trace; break;
        case LogLevel::Debug: sl = spdlog::level::debug; break;
        case LogLevel::Info:  sl = spdlog::level::info;  break;
        case LogLevel::Warn:  sl = spdlog::level::warn;  break;
        case LogLevel::Error: sl = spdlog::level::err;   break;
    }
    logger_->set_level(sl);
}

namespace {
void ffmpegLogCallback(void*, int level, const char* fmt, va_list vargs) {
    if (level > av_log_get_level()) return;
    char buf[1024];
    int  prefix = 1;
    av_log_format_line(nullptr, level, fmt, vargs, buf, sizeof(buf), &prefix);
    auto lg = nvr::Logger::instance().get();
    if (!lg) return;

    std::string msg(buf);
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.pop_back();

    if (level <= AV_LOG_ERROR)        lg->error("[ffmpeg] {}", msg);
    else if (level <= AV_LOG_WARNING) lg->warn ("[ffmpeg] {}", msg);
    else if (level <= AV_LOG_INFO)    lg->info ("[ffmpeg] {}", msg);
    else if (level <= AV_LOG_VERBOSE) lg->debug("[ffmpeg] {}", msg);
    else                              lg->trace("[ffmpeg] {}", msg);
}
}

void installFfmpegLogBridge() {
    av_log_set_callback(ffmpegLogCallback);
}

}
