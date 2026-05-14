#include "nvr/ArchiveManager.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>

namespace fs = std::filesystem;

TEST(ArchiveManager, EnforceQuotaSmoke) {
    auto root = fs::temp_directory_path() / "nvr_test_arch_quota";
    fs::remove_all(root);

    nvr::ArchiveConfig cfg;
    cfg.root_path          = root.string();
    cfg.segment_minutes    = 60;
    cfg.target_usage_ratio = 0.99;
    cfg.release_to_ratio   = 0.50;
    cfg.min_keep_minutes   = 0;
    cfg.file_prefix        = "nvr_";
    cfg.file_extension     = ".mp4";

    nvr::ArchiveManager mgr(cfg);
    mgr.start();
    mgr.enforceQuotaNow();
    mgr.stop();
    fs::remove_all(root);
}

TEST(ArchiveManager, ExtraRootsNextPath) {
    auto r1 = fs::temp_directory_path() / "nvr_test_arch_r1";
    auto r2 = fs::temp_directory_path() / "nvr_test_arch_r2";
    fs::remove_all(r1);
    fs::remove_all(r2);

    nvr::ArchiveConfig cfg;
    cfg.root_path             = r1.string();
    cfg.extra_archive_roots   = {r2.string()};
    cfg.segment_minutes       = 1;
    cfg.target_usage_ratio    = 0.99;
    cfg.release_to_ratio      = 0.50;
    cfg.min_keep_minutes      = 0;
    cfg.file_prefix           = "nvr_";
    cfg.file_extension        = ".mp4";

    nvr::ArchiveManager mgr(cfg);
    mgr.start();
    auto p = mgr.nextSegmentPath("camX", std::chrono::system_clock::now());
    const std::string ps = p.string();
    const std::string s1 = r1.string();
    const std::string s2 = r2.string();
    EXPECT_TRUE(ps.rfind(s1, 0) == 0 || ps.rfind(s2, 0) == 0);
    mgr.stop();
    fs::remove_all(r1);
    fs::remove_all(r2);
}

TEST(ArchiveManager, RegisterSegmentInfoOverload) {
    auto root = fs::temp_directory_path() / "nvr_test_arch_si";
    fs::remove_all(root);

    nvr::ArchiveConfig cfg;
    cfg.root_path          = root.string();
    cfg.segment_minutes    = 1;
    cfg.target_usage_ratio = 0.99;
    cfg.release_to_ratio   = 0.50;
    cfg.min_keep_minutes   = 0;
    cfg.file_prefix        = "nvr_";
    cfg.file_extension     = ".mp4";

    nvr::ArchiveManager mgr(cfg);
    mgr.start();
    auto path = mgr.nextSegmentPath("cam2", std::chrono::system_clock::now());
    std::ofstream(path) << "dummy";
    nvr::SegmentInfo si;
    si.camera_id  = "cam2";
    si.path        = path;
    si.started_at  = std::chrono::system_clock::now() - std::chrono::minutes(2);
    si.ended_at    = std::chrono::system_clock::now();
    si.size_bytes  = 5;
    si.has_motion  = false;
    mgr.registerSegment(si);
    mgr.stop();
    fs::remove_all(root);
}

TEST(ArchiveManager, RegisterAndQuery) {
    auto root = fs::temp_directory_path() / "nvr_test_arch";
    fs::remove_all(root);

    nvr::ArchiveConfig cfg;
    cfg.root_path           = root.string();
    cfg.segment_minutes     = 1;
    cfg.target_usage_ratio  = 0.80;
    cfg.release_to_ratio    = 0.70;
    cfg.min_keep_minutes    = 0;
    cfg.file_prefix         = "nvr_";
    cfg.file_extension      = ".mp4";

    nvr::ArchiveManager mgr(cfg);
    mgr.start();

    auto path = mgr.nextSegmentPath("cam1", std::chrono::system_clock::now());
    std::ofstream(path) << "dummy";
    mgr.registerSegment(path);

    EXPECT_GE(mgr.currentUsageRatio(), 0.0);
    mgr.stop();
    fs::remove_all(root);
}
