#include "nvr/store/Database.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace fs = std::filesystem;

TEST(Database, SequentialOpenPreservesSchemaVersion) {
    const auto p = fs::temp_directory_path() / "nvr_db_migrate_reopen.sqlite";
    for (const auto& ext : {"", "-wal", "-shm"}) {
        fs::remove(p.string() + ext);
    }
    int version = 0;
    {
        nvr::store::Database db(p);
        version = db.schemaVersion();
        EXPECT_GE(version, 1);
    }
    {
        nvr::store::Database db(p);
        EXPECT_EQ(db.schemaVersion(), version);
    }
    for (const auto& ext : {"", "-wal", "-shm"}) {
        fs::remove(p.string() + ext);
    }
}
