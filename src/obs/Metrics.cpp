#include "nvr/obs/Metrics.hpp"

#include <sstream>

namespace nvr::obs {

Registry& Registry::instance() {
    static Registry r;
    return r;
}

CameraMetrics& Registry::camera(const std::string& id) {
    {
        std::shared_lock<std::shared_mutex> lk(mu_);
        auto it = cams_.find(id);
        if (it != cams_.end()) return *it->second;
    }
    std::unique_lock<std::shared_mutex> lk(mu_);
    auto& slot = cams_[id];
    if (!slot) slot = std::make_unique<CameraMetrics>();
    return *slot;
}

std::string Registry::renderPrometheus() {
    std::ostringstream oss;

    auto print_counter = [&](const char* name, uint64_t v, const std::string& labels = {}) {
        oss << "# TYPE " << name << " counter\n";
        oss << name;
        if (!labels.empty()) oss << "{" << labels << "}";
        oss << " " << v << "\n";
    };
    auto print_gauge = [&](const char* name, double v, const std::string& labels = {}) {
        oss << "# TYPE " << name << " gauge\n";
        oss << name;
        if (!labels.empty()) oss << "{" << labels << "}";
        oss << " " << v << "\n";
    };

    std::shared_lock<std::shared_mutex> lk(mu_);

    print_gauge  ("nvr_uptime_seconds", static_cast<double>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_at_).count()));

    print_gauge  ("nvr_archive_used_ratio",            global_.archive_used_ratio.value());
    print_counter("nvr_archive_evictions_total",       global_.archive_evictions_total.value());
    print_gauge  ("nvr_archive_segments_total",        global_.archive_segments_total.value());
    print_counter("nvr_python_queue_dropped_total",    global_.python_queue_dropped_total.value());
    print_counter("nvr_analytics_jobs_enqueued_total",   global_.analytics_jobs_enqueued_total.value());
    print_counter("nvr_analytics_jobs_processed_total",  global_.analytics_jobs_processed_total.value());
    print_counter("nvr_analytics_queue_dropped_total",   global_.analytics_queue_dropped_total.value());
    print_counter("nvr_notifications_sent_total",      global_.notifications_sent_total.value());
    print_counter("nvr_notifications_failed_total",    global_.notifications_failed_total.value());
    print_counter("nvr_notifications_dropped_total",   global_.notifications_dropped_total.value());
    print_counter("nvr_http_requests_total",           global_.http_requests_total.value());

    for (const auto& [id, m] : cams_) {
        auto l = "camera=\"" + id + "\"";
        print_counter("nvr_camera_rtsp_reconnects_total", m->rtsp_reconnects_total.value(), l);
        print_counter("nvr_camera_decoder_errors_total",  m->decoder_errors_total.value(),  l);
        print_counter("nvr_camera_frames_received_total", m->frames_received_total.value(), l);
        print_counter("nvr_camera_frames_dropped_total",  m->frames_dropped_total.value(),  l);
        print_counter("nvr_camera_packets_written_total", m->packets_written_total.value(), l);
        print_counter("nvr_camera_bytes_in_total",        m->bytes_in_total.value(),        l);
        print_counter("nvr_camera_bytes_recorded_total",  m->bytes_recorded_total.value(),  l);
        print_counter("nvr_camera_motion_events_total",   m->motion_events_total.value(),   l);
        print_counter("nvr_camera_ai_inferences_total",   m->inferences_total.value(),      l);
        print_gauge  ("nvr_camera_inference_ms",          m->inference_ms.value(),          l);
        print_gauge  ("nvr_camera_inference_fps",         m->inference_fps.value(),         l);
        print_gauge  ("nvr_camera_fps",                   m->fps.value(),                   l);
        print_gauge  ("nvr_camera_bitrate_kbps",          m->bitrate_kbps.value(),          l);
        print_gauge  ("nvr_camera_pipeline_lag_ms",       m->pipeline_lag_ms.value(),       l);
        print_gauge  ("nvr_camera_state",                 m->state.value(),                 l);
    }

    return oss.str();
}

}
