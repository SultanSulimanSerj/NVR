#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace nvr::ai {

struct BBox { float x{0}, y{0}, w{0}, h{0}; };

struct Detection {
    std::string class_name;
    int64_t     track_id{-1};
    float       confidence{0};
    BBox        bbox;
    std::vector<float> embedding;
    std::chrono::system_clock::time_point ts{};
};

class IDetector {
public:
    virtual ~IDetector() = default;

    virtual bool load(const std::string& model_xml,
                      const std::string& device = "CPU") = 0;
    virtual bool ready() const noexcept = 0;

    virtual std::vector<Detection> infer(const uint8_t* bgr_data,
                                          int width, int height, int stride) = 0;

    virtual const std::string& kind()       const noexcept = 0;
    virtual float              lastInferMs() const noexcept = 0;
};

}
