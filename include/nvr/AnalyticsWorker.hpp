#pragma once

#include "nvr/EventBus.hpp"
#include "nvr/ThreadSafeQueue.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>

namespace nvr::store {
class Database;
}

namespace nvr {

class LicenseGate;

/// Lightweight job for the background analytics pipeline (no frames — metadata only).
struct AnalyticsJob {
    std::string camera_id;
    std::string type;
    std::string severity;
    std::string payload_json;
};

/// Bounded queue + dedicated thread: intake from `EventBus`, future license-gated ONNX / CV modules.
/// Today: copies `SystemEvent` into the queue (`DropOldest` on overflow), drains and counts metrics.
class AnalyticsWorker {
public:
    static constexpr size_t kDefaultQueueCapacity = 2048;

    AnalyticsWorker();
    ~AnalyticsWorker() { stop(); }

    /// Subscribe to `EventBus` before `start()` (idempotent if already started with same bus).
    void setEventBus(EventBus* bus) noexcept { bus_ = bus; }

    void setDatabase(store::Database* db) noexcept { db_ = db; }

    /// Optional: when set, ONVIF-derived `ai_detections` rows require licensed AI bit 0.
    void setLicenseGate(const LicenseGate* gate) noexcept { license_gate_ = gate; }

    void start();
    void stop();

    bool running() const noexcept { return running_.load(std::memory_order_relaxed); }

    /// For tests or future producers; enqueues a copy (truncates `payload_json`).
    bool tryEnqueue(AnalyticsJob job);

private:
    void threadMain_();
    void onBusEvent_(const SystemEvent& ev);
    void syncDropMetric_();

    EventBus*              bus_{nullptr};
    uint64_t               bus_sub_id_{0};

    store::Database*       db_{nullptr};
    const LicenseGate*     license_gate_{nullptr};

    std::unique_ptr<ThreadSafeQueue<AnalyticsJob>> queue_;
    std::atomic<bool>                              running_{false};
    std::thread                                     th_;
    std::atomic<uint64_t>                           last_drop_reported_{0};
};

} // namespace nvr
