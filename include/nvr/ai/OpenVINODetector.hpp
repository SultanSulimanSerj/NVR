#pragma once

#include "nvr/ai/IDetector.hpp"

#include <memory>
#include <string>

namespace nvr::ai {

class OpenVINODetector : public IDetector {
public:
    OpenVINODetector(std::string kind, std::vector<std::string> class_labels);
    ~OpenVINODetector() override;

    bool load(const std::string& model_xml, const std::string& device = "CPU") override;
    bool ready() const noexcept override { return ready_; }

    std::vector<Detection> infer(const uint8_t* bgr, int w, int h, int stride) override;

    const std::string& kind()       const noexcept override { return kind_; }
    float              lastInferMs() const noexcept override { return last_ms_; }

    void setConfidenceThreshold(float t) { conf_thr_ = t; }

private:
    struct Impl;
    std::unique_ptr<Impl>   impl_;
    std::string             kind_;
    std::vector<std::string> labels_;
    float                   conf_thr_{0.5f};
    bool                    ready_{false};
    float                   last_ms_{0.f};
};

}
