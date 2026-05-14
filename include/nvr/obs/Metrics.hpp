#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace nvr::obs {

class Counter {
public:
    void  inc(uint64_t n = 1) noexcept { v_.fetch_add(n, std::memory_order_relaxed); }
    uint64_t value() const noexcept    { return v_.load(std::memory_order_relaxed); }
private:
    std::atomic<uint64_t> v_{0};
};

class Gauge {
public:
    void   set(double v) noexcept   { v_.store(v, std::memory_order_relaxed); }
    void   add(double v) noexcept   {
        double cur = v_.load(std::memory_order_relaxed);
        while (!v_.compare_exchange_weak(cur, cur + v)) {}
    }
    double value() const noexcept   { return v_.load(std::memory_order_relaxed); }
private:
    std::atomic<double> v_{0.0};
};

struct CameraMetrics {
    Counter rtsp_reconnects_total;
    Counter decoder_errors_total;
    Counter frames_received_total;
    Counter frames_dropped_total;
    Counter packets_written_total;
    Counter bytes_in_total;
    Counter bytes_recorded_total;
    Counter motion_events_total;
    Counter inferences_total;
    Gauge   pipeline_lag_ms;
    Gauge   fps;
    Gauge   bitrate_kbps;
    Gauge   state;
    Gauge   inference_ms;
    Gauge   inference_fps;
};

struct GlobalMetrics {
    Gauge   archive_used_ratio;
    Counter archive_evictions_total;
    Gauge   archive_segments_total;
    Counter python_queue_dropped_total;
    Counter notifications_sent_total;
    Counter notifications_failed_total;
    Counter notifications_dropped_total;
    Counter http_requests_total;
    /// Background analytics worker (`AnalyticsWorker`): jobs from EventBus, future ONNX gate.
    Counter analytics_jobs_enqueued_total;
    Counter analytics_jobs_processed_total;
    Counter analytics_queue_dropped_total;
};

class Registry {
public:
    static Registry& instance();

    CameraMetrics& camera(const std::string& id);
    GlobalMetrics& global() noexcept { return global_; }

    std::string renderPrometheus();

private:
    Registry() = default;

    std::shared_mutex mu_;
    std::unordered_map<std::string, std::unique_ptr<CameraMetrics>> cams_;
    GlobalMetrics  global_;
    std::chrono::steady_clock::time_point start_at_{std::chrono::steady_clock::now()};
};

}
