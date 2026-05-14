#include "nvr/store/ConfigStore.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace fs = std::filesystem;

TEST(ConfigStore, UpsertAndList) {
    auto dir = fs::temp_directory_path() / "nvr_store_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    auto key = nvr::store::loadOrCreateMasterKey(dir / "master.key");
    nvr::store::Database db(dir / "db.sqlite");
    nvr::store::ConfigStore store(db, key);

    nvr::CameraConfig c;
    c.id = "cam-A";
    c.name = "A";
    c.rtsp_url = "rtsp://a";
    c.plan_x = 0.1;
    c.plan_y = 0.2;
    store.upsertCamera(c);

    auto list = store.listCameras();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].id, "cam-A");
    EXPECT_EQ(list[0].rtsp_url, "rtsp://a");
    EXPECT_DOUBLE_EQ(list[0].plan_x, 0.1);
    EXPECT_DOUBLE_EQ(list[0].plan_y, 0.2);

    store.deleteCamera("cam-A");
    EXPECT_TRUE(store.listCameras().empty());

    fs::remove_all(dir);
}
