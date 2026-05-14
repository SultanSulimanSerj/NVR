#pragma once

#include "nvr/Config.hpp"

#include <chrono>
#include <memory>

#if NVR_WITH_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/video/background_segm.hpp>
#endif

namespace nvr {

struct MotionResult {
    bool   motion{false};
    double area_ratio{0.0};
};

class MotionDetector {
public:
    explicit MotionDetector(MotionConfig cfg);

#if NVR_WITH_OPENCV
    MotionResult process(const cv::Mat& bgr_full);
    bool         saveSnapshot(const cv::Mat& bgr_full, const std::string& path) const;
    bool         shouldEmit() noexcept;
    cv::Size     downscaleSize() const noexcept;

    void         setRois(const std::vector<MotionRoi>& rois);
#endif

private:
    MotionConfig cfg_;
    std::chrono::steady_clock::time_point last_emit_{};
#if NVR_WITH_OPENCV
    cv::Ptr<cv::BackgroundSubtractorMOG2> bg_;
    cv::Mat scratch_small_;
    cv::Mat scratch_mask_;
    cv::Mat roi_mask_;
    int     roi_built_w_{0}, roi_built_h_{0};
    std::vector<MotionRoi> rois_;
#endif
};

}
