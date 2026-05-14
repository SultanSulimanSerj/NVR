#pragma once

#include "nvr/ai/IDetector.hpp"
#include "nvr/ai/Tracker.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace nvr::store { class Database; }

namespace nvr::ai {

class AiPipeline {
public:
    AiPipeline();
    ~AiPipeline();

    void setDatabase(store::Database* db) noexcept;

    void addDetector(std::shared_ptr<IDetector> det);

    struct InferResult {
        std::vector<Detection> dets;
        float                  inference_ms{0};
    };

    InferResult run(const std::string& camera_id,
                    const uint8_t* bgr, int w, int h, int stride);

    void   setTargetFps(int fps) noexcept;
    int    targetFps() const noexcept;

    bool   shouldRun() noexcept;

private:
    std::vector<std::shared_ptr<IDetector>> detectors_;
    IouTracker                              tracker_;
    store::Database*                        db_{nullptr};
    std::atomic<int>                        target_fps_{5};
    std::chrono::steady_clock::time_point   last_;
};

}
