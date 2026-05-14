#include "nvr/AnalyticsWorker.hpp"

#include "nvr/LicenseGate.hpp"
#include "nvr/Logger.hpp"
#include "nvr/obs/Metrics.hpp"
#include "nvr/store/Database.hpp"

#include <nlohmann/json.hpp>

#include <utility>

namespace nvr {

namespace {

constexpr size_t kMaxPayloadJson = 1024;

AnalyticsJob jobFromSystemEvent(const SystemEvent& ev) {
    AnalyticsJob j;
    j.camera_id    = ev.camera_id;
    j.type         = ev.type;
    j.severity     = ev.severity;
    j.payload_json = ev.payload_json;
    if (j.payload_json.size() > kMaxPayloadJson) j.payload_json.resize(kMaxPayloadJson);
    return j;
}

} // namespace

AnalyticsWorker::AnalyticsWorker() = default;

void AnalyticsWorker::setEventBus(EventBus* bus) noexcept {
    bus_ = bus;
}

bool AnalyticsWorker::tryEnqueue(AnalyticsJob job) {
    if (!queue_) return false;
    if (job.payload_json.size() > kMaxPayloadJson) job.payload_json.resize(kMaxPayloadJson);
    if (!queue_->tryPush(std::move(job))) return false;
    obs::Registry::instance().global().analytics_jobs_enqueued_total.inc(1);
    return true;
}

void AnalyticsWorker::onBusEvent_(const SystemEvent& ev) {
    if (!queue_) return;
    auto j = jobFromSystemEvent(ev);
    if (!queue_->tryPush(std::move(j))) {
        obs::Registry::instance().global().analytics_queue_dropped_total.inc(1);
        return;
    }
    obs::Registry::instance().global().analytics_jobs_enqueued_total.inc(1);
}

void AnalyticsWorker::syncDropMetric_() {
    if (!queue_) return;
    const uint64_t d = queue_->droppedCount();
    const uint64_t prev = last_drop_reported_.load(std::memory_order_relaxed);
    if (d > prev) {
        obs::Registry::instance().global().analytics_queue_dropped_total.inc(d - prev);
        last_drop_reported_.store(d, std::memory_order_relaxed);
    }
}

void AnalyticsWorker::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;

    queue_ = std::make_unique<ThreadSafeQueue<AnalyticsJob>>(kDefaultQueueCapacity, OverflowPolicy::DropOldest);
    last_drop_reported_.store(0, std::memory_order_relaxed);

    if (bus_ && bus_sub_id_ == 0) {
        bus_sub_id_ = bus_->subscribe([this](const SystemEvent& ev) { onBusEvent_(ev); });
        NVR_INFO("analytics", "subscribed to EventBus id=%llu",
                 static_cast<unsigned long long>(bus_sub_id_));
    }

    th_ = std::thread([this] { threadMain_(); });
    NVR_INFO("analytics", "worker started (queue_cap=%zu)", kDefaultQueueCapacity);
}

void AnalyticsWorker::stop() {
    running_.store(false, std::memory_order_release);

    if (bus_ && bus_sub_id_ != 0) {
        bus_->unsubscribe(bus_sub_id_);
        bus_sub_id_ = 0;
    }

    if (queue_) queue_->close();
    if (th_.joinable()) th_.join();
    queue_.reset();
}

void AnalyticsWorker::threadMain_() {
    using json = nlohmann::json;
    while (running_.load(std::memory_order_acquire)) {
        if (!queue_) break;
        auto opt = queue_->waitAndPop();
        if (!opt) break;
        syncDropMetric_();

        if (db_) {
            const auto& job = *opt;
            if (job.type == "motion.detected") {
                json p = json::parse(job.payload_json, nullptr, false);
                const double conf = p.value("area_ratio", 0.0);
                json         bb  = json::object();
                if (p.contains("snapshot_path")) bb["snapshot_path"] = p["snapshot_path"];
                try {
                    db_->insertAiDetection(job.camera_id, "motion", conf, 0, bb.dump());
                } catch (const std::exception& e) {
                    NVR_WARN("analytics", "ai_detections insert: %s", e.what());
                }
            } else if (job.type.rfind("onvif.", 0) == 0) {
                if (license_gate_ && !license_gate_->allowAiModule(0)) {
                    NVR_INFO("analytics", "skip onvif ai_detection (license bit 0): %s", job.type.c_str());
                } else {
                    json p = json::parse(job.payload_json, nullptr, false);
                    json bb  = json::object();
                    if (p.contains("topic")) bb["topic"] = p["topic"];
                    try {
                        db_->insertAiDetection(job.camera_id, job.type, 1.0, 0, bb.dump());
                    } catch (const std::exception& e) {
                        NVR_WARN("analytics", "onvif ai_detections insert: %s", e.what());
                    }
                }
            }
        }

        obs::Registry::instance().global().analytics_jobs_processed_total.inc(1);
    }
    syncDropMetric_();
}

} // namespace nvr
