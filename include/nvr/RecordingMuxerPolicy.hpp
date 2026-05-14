#pragma once

#include "nvr/Config.hpp"

#include <chrono>

namespace nvr {

/// Motion-based recording is active only when recording is on, mode is motion, and the
/// motion detector path is available (OpenCV build + enable_motion in pipeline).
inline bool motionRecordingActive(bool enable_recording, RecordingMode mode,
                                   bool motion_detector_available) noexcept {
    return enable_recording && mode == RecordingMode::Motion && motion_detector_available;
}

/// While motion recording: write only inside the post-event latch window (or always if continuous).
inline bool motionLatchAllowsWrite(bool motion_record,
                                   std::chrono::steady_clock::time_point now,
                                   std::chrono::steady_clock::time_point latch_until) noexcept {
    if (!motion_record) return true;
    return now < latch_until;
}

/// Close the mux segment on a keyframe when writing must stop (latch, schedule, etc.).
inline bool shouldFinalizeRecordingOnKeyframe(bool mux_allows_write, bool writer_open,
                                               bool is_keyframe) noexcept {
    return !mux_allows_write && writer_open && is_keyframe;
}

/// Same as `shouldFinalizeRecordingOnKeyframe` with motion latch only (legacy tests / call sites).
inline bool shouldFinalizeMotionSegmentOnKeyframe(bool motion_record, bool latch_allows_write,
                                                    bool writer_open, bool is_keyframe) noexcept {
    const bool mux = !motion_record || latch_allows_write;
    return shouldFinalizeRecordingOnKeyframe(mux, writer_open, is_keyframe);
}

/// Whether mux may accept packets (continuous: schedule only; motion: latch ∧ schedule).
inline bool mayWriteRecordingPacket(bool motion_record, bool latch_allows_write,
                                    bool schedule_allows_write) noexcept {
    return schedule_allows_write && (!motion_record || latch_allows_write);
}

/// After opening a motion segment, skip non-keyframes until the first keyframe.
inline bool skipWriteUntilNextKeyframe(bool motion_record, bool motion_need_keyframe,
                                       bool is_keyframe) noexcept {
    return motion_record && motion_need_keyframe && !is_keyframe;
}

} // namespace nvr
