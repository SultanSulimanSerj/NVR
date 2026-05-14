#include "nvr/RecordingMuxerPolicy.hpp"

#include <gtest/gtest.h>

using namespace nvr;
using clk = std::chrono::steady_clock;

TEST(RecordingMuxerPolicy, MotionRecordingActiveRequiresDetector) {
    EXPECT_FALSE(motionRecordingActive(true, RecordingMode::Motion, false));
    EXPECT_TRUE(motionRecordingActive(true, RecordingMode::Motion, true));
    EXPECT_FALSE(motionRecordingActive(false, RecordingMode::Motion, true));
    EXPECT_FALSE(motionRecordingActive(true, RecordingMode::Continuous, false));
    EXPECT_FALSE(motionRecordingActive(true, RecordingMode::Hybrid, true));
}

TEST(RecordingMuxerPolicy, LatchAndFinalize) {
    const auto t0 = clk::now();
    const auto t1 = t0 + std::chrono::seconds(10);

    EXPECT_TRUE(motionLatchAllowsWrite(false, t0, t1));
    EXPECT_TRUE(motionLatchAllowsWrite(true, t0, t1));
    EXPECT_FALSE(motionLatchAllowsWrite(true, t1, t0));

    EXPECT_FALSE(shouldFinalizeRecordingOnKeyframe(true, true, true));
    EXPECT_TRUE(shouldFinalizeRecordingOnKeyframe(false, true, true));
    EXPECT_FALSE(shouldFinalizeRecordingOnKeyframe(false, false, true));
    EXPECT_FALSE(shouldFinalizeRecordingOnKeyframe(false, true, false));

    EXPECT_FALSE(shouldFinalizeMotionSegmentOnKeyframe(true, true, true, true));
    EXPECT_TRUE(shouldFinalizeMotionSegmentOnKeyframe(true, false, true, true));
    EXPECT_FALSE(shouldFinalizeMotionSegmentOnKeyframe(true, false, false, true));
    EXPECT_FALSE(shouldFinalizeMotionSegmentOnKeyframe(true, false, true, false));

    EXPECT_TRUE(mayWriteRecordingPacket(false, false, true));
    EXPECT_TRUE(mayWriteRecordingPacket(false, true, true));
    EXPECT_TRUE(mayWriteRecordingPacket(true, true, true));
    EXPECT_FALSE(mayWriteRecordingPacket(true, false, true));
    EXPECT_FALSE(mayWriteRecordingPacket(true, true, false));
    EXPECT_FALSE(mayWriteRecordingPacket(false, true, false));

    EXPECT_TRUE(skipWriteUntilNextKeyframe(false, true, false));
    EXPECT_TRUE(skipWriteUntilNextKeyframe(true, true, false));
    EXPECT_FALSE(skipWriteUntilNextKeyframe(true, true, true));
    EXPECT_FALSE(skipWriteUntilNextKeyframe(true, false, false));
}
