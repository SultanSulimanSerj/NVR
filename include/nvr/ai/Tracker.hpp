#pragma once

#include "nvr/ai/IDetector.hpp"

#include <cstdint>
#include <vector>

namespace nvr::ai {

class IouTracker {
public:
    explicit IouTracker(float iou_thr = 0.3f, int max_age = 30);

    void assign(std::vector<Detection>& dets);

private:
    struct Track {
        int64_t id{};
        BBox    bbox;
        int     age{0};
        int     missed{0};
    };

    std::vector<Track> tracks_;
    int64_t            next_id_{1};
    float              iou_thr_;
    int                max_age_;
};

}
