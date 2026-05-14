#include "nvr/ai/AiPipeline.hpp"

#include "nvr/Logger.hpp"
#include "nvr/obs/Metrics.hpp"
#include "nvr/store/Database.hpp"

#include <nlohmann/json.hpp>

namespace nvr::ai {

using json = nlohmann::json;

AiPipeline::AiPipeline() : last_(std::chrono::steady_clock::now()) {}
AiPipeline::~AiPipeline() = default;

void AiPipeline::setDatabase(store::Database* db) noexcept { db_ = db; }

void AiPipeline::addDetector(std::shared_ptr<IDetector> det) {
    detectors_.push_back(std::move(det));
}

void AiPipeline::setTargetFps(int fps) noexcept { target_fps_.store(std::max(1, fps)); }
int  AiPipeline::targetFps() const noexcept     { return target_fps_.load(); }

bool AiPipeline::shouldRun() noexcept {
    auto now    = std::chrono::steady_clock::now();
    auto period = std::chrono::milliseconds(1000 / target_fps_.load());
    if (now - last_ < period) return false;
    last_ = now;
    return true;
}

AiPipeline::InferResult AiPipeline::run(const std::string& camera_id,
                                          const uint8_t* bgr, int w, int h, int stride) {
    InferResult r;
    if (detectors_.empty()) return r;

    float total_ms = 0;
    for (auto& d : detectors_) {
        if (!d->ready()) continue;
        auto out = d->infer(bgr, w, h, stride);
        total_ms += d->lastInferMs();
        for (auto& det : out) r.dets.push_back(std::move(det));
    }
    r.inference_ms = total_ms;
    tracker_.assign(r.dets);

    obs::Registry::instance().camera(camera_id).inferences_total.inc(r.dets.size());

    if (db_) {
        std::lock_guard<std::recursive_mutex> lk(db_->mutex());
        for (auto& d : r.dets) {
            json bb = {{"x", d.bbox.x}, {"y", d.bbox.y}, {"w", d.bbox.w}, {"h", d.bbox.h}};
            SQLite::Statement q(db_->raw(),
                "INSERT INTO ai_detections(camera_id, object_type, confidence, track_id, bbox_json) "
                "VALUES(?, ?, ?, ?, ?)");
            q.bind(1, camera_id);
            q.bind(2, d.class_name);
            q.bind(3, d.confidence);
            q.bind(4, static_cast<int64_t>(d.track_id));
            q.bind(5, bb.dump());
            try { q.exec(); } catch (const std::exception& e) {
                NVR_WARN("ai", "store: %s", e.what());
            }
        }
    }
    return r;
}

}
