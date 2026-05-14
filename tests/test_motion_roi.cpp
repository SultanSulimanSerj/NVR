#include "nvr/Config.hpp"

#include <gtest/gtest.h>

TEST(MotionRoi, EmptyByDefault) {
    nvr::CameraConfig c;
    EXPECT_TRUE(c.motion_rois.empty());
}

TEST(MotionRoi, AssignPolygon) {
    nvr::MotionRoi roi;
    roi.name = "z1";
    roi.polygon_norm = {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}};
    EXPECT_EQ(roi.polygon_norm.size(), 4u);
}
