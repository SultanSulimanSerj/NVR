#pragma once

#include <SQLiteCpp/SQLiteCpp.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace nvr::store {

class Database {
public:
    explicit Database(const std::filesystem::path& path);

    SQLite::Database& raw() noexcept { return *db_; }
    std::recursive_mutex& mutex() noexcept { return mu_; }

    int  schemaVersion();
    void migrate();

    /// Best-effort housekeeping for expired auth rows and old DLQ entries.
    void purgeExpiredAuthData();

    void setSetting(const std::string& key, const std::string& value_json);
    std::string getSetting(const std::string& key, const std::string& def = "");

    void audit(const std::string& user_login, const std::string& action,
               const std::string& target = {}, const std::string& payload_json = "{}");

    int64_t insertSegment(const std::string& camera_id, const std::string& path,
                          const std::string& started_at, const std::string& ended_at,
                          int64_t duration_ms, int64_t size_bytes, bool has_motion);
    void    deleteSegmentByPath(const std::string& path);
    int64_t insertEvent(const std::string& camera_id, const std::string& type,
                        const std::string& severity, const std::string& payload_json,
                        const std::string& snapshot_path, const std::string& clip_path);
    bool    ackEvent(int64_t id, const std::string& user);

    int64_t insertAiDetection(const std::string& camera_id, const std::string& object_type,
                              double confidence, int64_t track_id, const std::string& bbox_json);

    /// JSON array `[{id,name,payload,updated_at},…]` for kiosk live grid presets.
    std::string listLiveLayoutsJson(const std::string& user_login);
    void        upsertLiveLayout(const std::string& user_login, const std::string& name,
                                 const std::string& payload_json);
    bool        deleteLiveLayout(const std::string& user_login, const std::string& name);

private:
    std::unique_ptr<SQLite::Database> db_;
    std::recursive_mutex              mu_;
};

}
