#include "nvr/Config.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST(Config, ArchiveRootPathFromYaml) {
    const auto p = fs::temp_directory_path() / "nvr_cfg_archive.yaml";
    {
        std::ofstream o(p);
        o << "archive:\n"
             "  root_path: /data/nvr/custom_archive\n"
             "  segment_minutes: 30\n";
    }
    const auto cfg = nvr::loadConfig(p.string());
    EXPECT_EQ(cfg.archive.root_path, "/data/nvr/custom_archive");
    EXPECT_EQ(cfg.archive.segment_minutes, 30);
    fs::remove(p);
}
