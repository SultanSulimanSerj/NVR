#include "nvr/MotionDetector.hpp"

#include "nvr/Logger.hpp"

#include <filesystem>

#if NVR_WITH_OPENCV
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace fs = std::filesystem;

namespace nvr {

MotionDetector::MotionDetector(MotionConfig cfg) : cfg_(std::move(cfg)) {
#if NVR_WITH_OPENCV
    bg_ = cv::createBackgroundSubtractorMOG2(cfg_.history, cfg_.var_threshold,
                                              cfg_.detect_shadows);
#endif
    std::error_code ec;
    fs::create_directories(cfg_.snapshot_dir, ec);
    if (ec) {
        NVR_WARN("motion", "could not create snapshot dir '%s': %s",
                 cfg_.snapshot_dir.c_str(), ec.message().c_str());
    }
}

#if NVR_WITH_OPENCV

cv::Size MotionDetector::downscaleSize() const noexcept {
    return {cfg_.downscale_width, cfg_.downscale_height};
}

void MotionDetector::setRois(const std::vector<MotionRoi>& rois) {
    rois_         = rois;
    roi_built_w_  = 0;
    roi_built_h_  = 0;
    roi_mask_.release();
}

MotionResult MotionDetector::process(const cv::Mat& bgr_full) {
    MotionResult r;
    if (bgr_full.empty()) return r;

    cv::resize(bgr_full, scratch_small_, downscaleSize(), 0, 0, cv::INTER_AREA);
    bg_->apply(scratch_small_, scratch_mask_);

    cv::threshold(scratch_mask_, scratch_mask_, 200, 255, cv::THRESH_BINARY);
    cv::medianBlur(scratch_mask_, scratch_mask_, 5);

    if (!rois_.empty()) {
        if (roi_built_w_ != scratch_small_.cols || roi_built_h_ != scratch_small_.rows) {
            roi_mask_ = cv::Mat::zeros(scratch_small_.rows, scratch_small_.cols, CV_8UC1);
            for (const auto& roi : rois_) {
                std::vector<cv::Point> poly;
                poly.reserve(roi.polygon_norm.size());
                for (auto& [x, y] : roi.polygon_norm) {
                    poly.emplace_back(static_cast<int>(x * scratch_small_.cols),
                                       static_cast<int>(y * scratch_small_.rows));
                }
                if (poly.size() >= 3) {
                    const cv::Point* pts = poly.data();
                    int npts = static_cast<int>(poly.size());
                    cv::fillPoly(roi_mask_, &pts, &npts, 1, cv::Scalar(255));
                }
            }
            roi_built_w_ = scratch_small_.cols;
            roi_built_h_ = scratch_small_.rows;
        }
        if (!roi_mask_.empty()) {
            cv::bitwise_and(scratch_mask_, roi_mask_, scratch_mask_);
        }
    }

    const auto nz   = cv::countNonZero(scratch_mask_);
    const auto area = static_cast<double>(scratch_small_.cols * scratch_small_.rows);

    r.area_ratio = (area > 0.0) ? static_cast<double>(nz) / area : 0.0;
    r.motion     = r.area_ratio >= cfg_.min_area_ratio;
    return r;
}

bool MotionDetector::saveSnapshot(const cv::Mat& bgr_full, const std::string& path) const {
    std::vector<int> params{cv::IMWRITE_JPEG_QUALITY, cfg_.snapshot_jpeg_quality};
    try {
        return cv::imwrite(path, bgr_full, params);
    } catch (const cv::Exception& e) {
        NVR_WARN("motion", "imwrite failed: %s", e.what());
        return false;
    }
}

bool MotionDetector::shouldEmit() noexcept {
    auto now = std::chrono::steady_clock::now();
    if (now - last_emit_ < std::chrono::seconds(cfg_.cooldown_seconds)) return false;
    last_emit_ = now;
    return true;
}

#endif

}
