#include "nvr/Config.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST(Config, LoadDefaultsAndCameras) {
    auto p = fs::temp_directory_path() / "nvr_cfg.yaml";
    {
        std::ofstream o(p);
        o << "logging_level: debug\n"
             "cameras:\n"
             "  - id: c1\n"
             "    rtsp_url: rtsp://x\n"
             "    preferred_hw: vaapi\n"
             "    recording_mode: motion\n";
    }
    auto cfg = nvr::loadConfig(p.string());
    EXPECT_EQ(cfg.logging_level, "debug");
    ASSERT_EQ(cfg.cameras.size(), 1u);
    EXPECT_EQ(cfg.cameras[0].id, "c1");
    EXPECT_EQ(cfg.cameras[0].preferred_hw, nvr::HwAccel::VAAPI);
    EXPECT_EQ(cfg.cameras[0].recording_mode, nvr::RecordingMode::Motion);
    fs::remove(p);
}

TEST(Config, RecordingModeParseDefaults) {
    EXPECT_EQ(nvr::parseRecordingMode(""), nvr::RecordingMode::Continuous);
    EXPECT_EQ(nvr::parseRecordingMode("continuous"), nvr::RecordingMode::Continuous);
    EXPECT_EQ(nvr::parseRecordingMode("MOTION"), nvr::RecordingMode::Motion);
    EXPECT_EQ(nvr::parseRecordingMode("hybrid"), nvr::RecordingMode::Hybrid);
    EXPECT_STREQ(nvr::recordingModeToString(nvr::RecordingMode::Continuous), "continuous");
    EXPECT_STREQ(nvr::recordingModeToString(nvr::RecordingMode::Motion), "motion");
    EXPECT_STREQ(nvr::recordingModeToString(nvr::RecordingMode::Hybrid), "hybrid");
}

TEST(Config, RecordingModeYamlDefaultContinuous) {
    auto p = fs::temp_directory_path() / "nvr_cfg_rec_default.yaml";
    {
        std::ofstream o(p);
        o << "cameras:\n"
             "  - id: c2\n"
             "    rtsp_url: rtsp://y\n";
    }
    auto cfg = nvr::loadConfig(p.string());
    ASSERT_EQ(cfg.cameras.size(), 1u);
    EXPECT_EQ(cfg.cameras[0].recording_mode, nvr::RecordingMode::Continuous);
    fs::remove(p);
}

TEST(Config, RecordingModeYamlHybrid) {
    auto p = fs::temp_directory_path() / "nvr_cfg_hybrid.yaml";
    {
        std::ofstream o(p);
        o << "cameras:\n"
             "  - id: c_hyb\n"
             "    rtsp_url: rtsp://z\n"
             "    recording_mode: hybrid\n";
    }
    auto cfg = nvr::loadConfig(p.string());
    ASSERT_EQ(cfg.cameras.size(), 1u);
    EXPECT_EQ(cfg.cameras[0].recording_mode, nvr::RecordingMode::Hybrid);
    fs::remove(p);
}

TEST(Config, RecordingScheduleYamlInvalid) {
    auto p = fs::temp_directory_path() / "nvr_cfg_bad_sched.yaml";
    {
        std::ofstream o(p);
        o << "cameras:\n"
             "  - id: c3\n"
             "    rtsp_url: rtsp://z\n"
             "    recording_schedule_json: \"not json\"\n";
    }
    EXPECT_THROW(nvr::loadConfig(p.string()), std::runtime_error);
    fs::remove(p);
}

TEST(Config, RecordingScheduleYamlValid) {
    auto p = fs::temp_directory_path() / "nvr_cfg_ok_sched.yaml";
    {
        std::ofstream o(p);
        o << "cameras:\n"
             "  - id: c4\n"
             "    rtsp_url: rtsp://z\n"
             "    recording_schedule_json: "
             "'{\"always\":false,\"weekdays\":{\"mon\":[{\"start\":\"09:00\",\"end\":\"17:00\"}],"
             "\"tue\":[],\"wed\":[],\"thu\":[],\"fri\":[],\"sat\":[],\"sun\":[]}}'\n";
    }
    auto cfg = nvr::loadConfig(p.string());
    ASSERT_EQ(cfg.cameras.size(), 1u);
    EXPECT_NE(cfg.cameras[0].recording_schedule_json.find("\"always\":false"), std::string::npos);
    fs::remove(p);
}

TEST(Config, PlanCoordinatesYamlOptional) {
    auto p = fs::temp_directory_path() / "nvr_cfg_plan.yaml";
    {
        std::ofstream o(p);
        o << "cameras:\n"
             "  - id: c5\n"
             "    rtsp_url: rtsp://z\n"
             "    plan_x: 0.25\n"
             "    plan_y: 0.75\n";
    }
    auto cfg = nvr::loadConfig(p.string());
    ASSERT_EQ(cfg.cameras.size(), 1u);
    EXPECT_DOUBLE_EQ(cfg.cameras[0].plan_x, 0.25);
    EXPECT_DOUBLE_EQ(cfg.cameras[0].plan_y, 0.75);
    fs::remove(p);
}
