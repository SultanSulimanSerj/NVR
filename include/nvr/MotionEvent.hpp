#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace nvr {

struct MotionEvent {
    std::string                            camera_id;
    std::chrono::system_clock::time_point  timestamp;
    double                                 area_ratio{0.0};
    std::string                            snapshot_path;

    bool                                   include_frame{false};
    int                                    width{0};
    int                                    height{0};
    int                                    channels{3};
    std::vector<uint8_t>                   bgr_pixels;
};

}
