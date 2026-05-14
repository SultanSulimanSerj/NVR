#include "nvr/store/Database.hpp"

#include "nvr/Logger.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace nvr::store {

namespace {

constexpr std::string_view kMigration001 = R"SQL(
CREATE TABLE IF NOT EXISTS schema_version (version INTEGER PRIMARY KEY);
CREATE TABLE IF NOT EXISTS cameras (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    rtsp_url TEXT NOT NULL,
    sub_rtsp_url TEXT DEFAULT '',
    preferred_hw TEXT DEFAULT 'auto',
    analysis_fps INTEGER DEFAULT 6,
    enable_motion INTEGER DEFAULT 1,
    enable_recording INTEGER DEFAULT 1,
    enable_substream INTEGER DEFAULT 1,
    onvif_host TEXT DEFAULT '',
    onvif_port INTEGER DEFAULT 80,
    onvif_user TEXT DEFAULT '',
    onvif_pass_enc BLOB,
    motion_roi_json TEXT DEFAULT '[]',
    pre_event_seconds INTEGER DEFAULT 5,
    post_event_seconds INTEGER DEFAULT 10,
    sub_bitrate_kbps INTEGER DEFAULT 512,
    sub_width INTEGER DEFAULT 640,
    sub_height INTEGER DEFAULT 360,
    sub_fps INTEGER DEFAULT 15,
    disabled INTEGER DEFAULT 0,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE TABLE IF NOT EXISTS segments (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    camera_id TEXT NOT NULL,
    path TEXT NOT NULL,
    started_at TEXT NOT NULL,
    ended_at TEXT NOT NULL,
    duration_ms INTEGER NOT NULL,
    size_bytes INTEGER NOT NULL,
    has_motion INTEGER DEFAULT 0,
    FOREIGN KEY(camera_id) REFERENCES cameras(id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_segments_cam_start ON segments(camera_id, started_at);
CREATE TABLE IF NOT EXISTS events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    camera_id TEXT,
    ts TEXT NOT NULL,
    type TEXT NOT NULL,
    severity TEXT NOT NULL DEFAULT 'info',
    payload_json TEXT DEFAULT '{}',
    snapshot_path TEXT,
    clip_path TEXT,
    acknowledged INTEGER DEFAULT 0,
    dispatched INTEGER DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_events_ts ON events(ts);
CREATE INDEX IF NOT EXISTS idx_events_camera_ts ON events(camera_id, ts);
CREATE INDEX IF NOT EXISTS idx_events_type ON events(type);
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    login TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    role TEXT NOT NULL DEFAULT 'viewer',
    totp_secret_enc BLOB,
    disabled INTEGER DEFAULT 0,
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE TABLE IF NOT EXISTS settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS audit_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts TEXT NOT NULL DEFAULT (datetime('now')),
    user_login TEXT,
    action TEXT NOT NULL,
    target TEXT,
    payload TEXT
);
CREATE TABLE IF NOT EXISTS notification_channels (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    kind TEXT NOT NULL,
    name TEXT NOT NULL,
    config_enc BLOB,
    enabled INTEGER DEFAULT 1
);
CREATE TABLE IF NOT EXISTS notification_rules (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    camera_id TEXT,
    event_type TEXT NOT NULL,
    severity_min TEXT NOT NULL DEFAULT 'info',
    throttle_seconds INTEGER NOT NULL DEFAULT 30,
    channel_id INTEGER NOT NULL,
    FOREIGN KEY(channel_id) REFERENCES notification_channels(id) ON DELETE CASCADE
);
INSERT OR IGNORE INTO schema_version(version) VALUES(1);
)SQL";

}

Database::Database(const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    db_ = std::make_unique<SQLite::Database>(
        path.string(),
        SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE | SQLite::OPEN_FULLMUTEX);
    db_->exec("PRAGMA journal_mode=WAL;");
    db_->exec("PRAGMA synchronous=NORMAL;");
    db_->exec("PRAGMA foreign_keys=ON;");
    db_->exec("PRAGMA busy_timeout=5000;");
    NVR_INFO("db", "opened %s", path.c_str());
    migrate();
}

int Database::schemaVersion() {
    try {
        SQLite::Statement q(*db_, "SELECT max(version) FROM schema_version");
        if (q.executeStep()) return q.getColumn(0).getInt();
    } catch (...) {}
    return 0;
}

namespace {
constexpr const char* kMigration002 = R"SQL(
CREATE TABLE IF NOT EXISTS face_persons (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    embedding BLOB,
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE TABLE IF NOT EXISTS ai_models (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    kind TEXT NOT NULL, path TEXT NOT NULL,
    precision TEXT DEFAULT 'FP16', device TEXT DEFAULT 'CPU', enabled INTEGER DEFAULT 1
);
CREATE TABLE IF NOT EXISTS ai_detections (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id INTEGER, camera_id TEXT NOT NULL,
    ts TEXT NOT NULL DEFAULT (datetime('now')),
    object_type TEXT NOT NULL, confidence REAL,
    track_id INTEGER, bbox_json TEXT, extra_json TEXT
);
CREATE INDEX IF NOT EXISTS idx_ai_camera_ts ON ai_detections(camera_id, ts);
CREATE INDEX IF NOT EXISTS idx_ai_object    ON ai_detections(object_type);
INSERT OR IGNORE INTO schema_version(version) VALUES(2);
)SQL";

// P0/P1: real TOTP encryption (pending vs active),
//         JWT revocation (jti + per-user token_version), refresh-token table,
//         atomic archive finalization marker, notification dead-letter.
constexpr const char* kMigration003 = R"SQL(
ALTER TABLE users ADD COLUMN totp_pending_enc BLOB;
ALTER TABLE users ADD COLUMN token_version INTEGER NOT NULL DEFAULT 1;
ALTER TABLE users ADD COLUMN updated_at TEXT NOT NULL DEFAULT (datetime('now'));
ALTER TABLE segments ADD COLUMN finalized INTEGER NOT NULL DEFAULT 0;
CREATE INDEX IF NOT EXISTS idx_segments_finalized ON segments(finalized);
CREATE TABLE IF NOT EXISTS token_revocations (
    jti TEXT PRIMARY KEY,
    expires_at TEXT NOT NULL,
    reason TEXT
);
CREATE INDEX IF NOT EXISTS idx_token_revocations_exp ON token_revocations(expires_at);
CREATE TABLE IF NOT EXISTS refresh_tokens (
    jti TEXT PRIMARY KEY,
    user_login TEXT NOT NULL,
    issued_at TEXT NOT NULL DEFAULT (datetime('now')),
    expires_at TEXT NOT NULL,
    revoked INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_refresh_tokens_user ON refresh_tokens(user_login);
CREATE TABLE IF NOT EXISTS notify_dead_letter (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts TEXT NOT NULL DEFAULT (datetime('now')),
    channel_id INTEGER,
    rule_id INTEGER,
    attempts INTEGER NOT NULL DEFAULT 0,
    last_error TEXT,
    payload_json TEXT
);
INSERT OR IGNORE INTO schema_version(version) VALUES(3);
)SQL";

// Helper: idempotently add a column only if it doesn't already exist (SQLite has no IF NOT EXISTS for ADD COLUMN).
bool columnExists(SQLite::Database& db, const std::string& table, const std::string& column) {
    SQLite::Statement q(db, "PRAGMA table_info(" + table + ")");
    while (q.executeStep()) {
        if (q.getColumn(1).getString() == column) return true;
    }
    return false;
}

}

void Database::migrate() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    {
        SQLite::Transaction tx(*db_);
        db_->exec(std::string(kMigration001));
        tx.commit();
    }
    if (schemaVersion() < 2) {
        SQLite::Transaction tx(*db_);
        db_->exec(std::string(kMigration002));
        tx.commit();
    }
    if (schemaVersion() < 3) {
        try {
            db_->exec("SAVEPOINT migrate_v3");
            if (!columnExists(*db_, "users",    "totp_pending_enc"))
                db_->exec("ALTER TABLE users    ADD COLUMN totp_pending_enc BLOB");
            if (!columnExists(*db_, "users",    "token_version"))
                db_->exec("ALTER TABLE users    ADD COLUMN token_version INTEGER NOT NULL DEFAULT 1");
            if (!columnExists(*db_, "users",    "updated_at"))
                db_->exec("ALTER TABLE users    ADD COLUMN updated_at TEXT NOT NULL DEFAULT (datetime('now'))");
            if (!columnExists(*db_, "segments", "finalized"))
                db_->exec("ALTER TABLE segments ADD COLUMN finalized INTEGER NOT NULL DEFAULT 0");
            db_->exec("CREATE INDEX IF NOT EXISTS idx_segments_finalized ON segments(finalized)");
            db_->exec(
                "CREATE TABLE IF NOT EXISTS token_revocations ("
                " jti TEXT PRIMARY KEY,"
                " expires_at TEXT NOT NULL,"
                " reason TEXT)");
            db_->exec("CREATE INDEX IF NOT EXISTS idx_token_revocations_exp ON token_revocations(expires_at)");
            db_->exec(
                "CREATE TABLE IF NOT EXISTS refresh_tokens ("
                " jti TEXT PRIMARY KEY,"
                " user_login TEXT NOT NULL,"
                " issued_at TEXT NOT NULL DEFAULT (datetime('now')),"
                " expires_at TEXT NOT NULL,"
                " revoked INTEGER NOT NULL DEFAULT 0)");
            db_->exec("CREATE INDEX IF NOT EXISTS idx_refresh_tokens_user ON refresh_tokens(user_login)");
            db_->exec(
                "CREATE TABLE IF NOT EXISTS notify_dead_letter ("
                " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                " ts TEXT NOT NULL DEFAULT (datetime('now')),"
                " channel_id INTEGER,"
                " rule_id INTEGER,"
                " attempts INTEGER NOT NULL DEFAULT 0,"
                " last_error TEXT,"
                " payload_json TEXT)");
            db_->exec("INSERT OR IGNORE INTO schema_version(version) VALUES(3)");
            db_->exec("RELEASE migrate_v3");
        } catch (const std::exception& e) {
            try {
                db_->exec("ROLLBACK TO migrate_v3");
                db_->exec("RELEASE migrate_v3");
            } catch (...) {}
            throw std::runtime_error(std::string("migration v3 failed: ") + e.what());
        }
    }
    if (schemaVersion() < 4) {
        try {
            db_->exec("SAVEPOINT migrate_v4");
            if (!columnExists(*db_, "cameras", "recording_mode"))
                db_->exec(
                    "ALTER TABLE cameras ADD COLUMN recording_mode TEXT NOT NULL DEFAULT 'continuous'");
            db_->exec("INSERT OR IGNORE INTO schema_version(version) VALUES(4)");
            db_->exec("RELEASE migrate_v4");
        } catch (const std::exception& e) {
            try {
                db_->exec("ROLLBACK TO migrate_v4");
                db_->exec("RELEASE migrate_v4");
            } catch (...) {}
            throw std::runtime_error(std::string("migration v4 failed: ") + e.what());
        }
    }
    if (schemaVersion() < 5) {
        try {
            db_->exec("SAVEPOINT migrate_v5");
            if (!columnExists(*db_, "cameras", "recording_schedule_json"))
                db_->exec(
                    "ALTER TABLE cameras ADD COLUMN recording_schedule_json TEXT NOT NULL "
                    "DEFAULT '{\"always\":true}'");
            db_->exec("INSERT OR IGNORE INTO schema_version(version) VALUES(5)");
            db_->exec("RELEASE migrate_v5");
        } catch (const std::exception& e) {
            try {
                db_->exec("ROLLBACK TO migrate_v5");
                db_->exec("RELEASE migrate_v5");
            } catch (...) {}
            throw std::runtime_error(std::string("migration v5 failed: ") + e.what());
        }
    }
    if (schemaVersion() < 6) {
        try {
            db_->exec("SAVEPOINT migrate_v6");
            if (!columnExists(*db_, "cameras", "plan_x"))
                db_->exec("ALTER TABLE cameras ADD COLUMN plan_x REAL NOT NULL DEFAULT -1");
            if (!columnExists(*db_, "cameras", "plan_y"))
                db_->exec("ALTER TABLE cameras ADD COLUMN plan_y REAL NOT NULL DEFAULT -1");
            db_->exec("INSERT OR IGNORE INTO schema_version(version) VALUES(6)");
            db_->exec("RELEASE migrate_v6");
        } catch (const std::exception& e) {
            try {
                db_->exec("ROLLBACK TO migrate_v6");
                db_->exec("RELEASE migrate_v6");
            } catch (...) {}
            throw std::runtime_error(std::string("migration v6 failed: ") + e.what());
        }
    }
    if (schemaVersion() < 7) {
        try {
            db_->exec("SAVEPOINT migrate_v7");
            db_->exec(
                "CREATE TABLE IF NOT EXISTS camera_acl ("
                " user_login TEXT NOT NULL,"
                " camera_id TEXT NOT NULL,"
                " PRIMARY KEY (user_login, camera_id))");
            db_->exec("CREATE INDEX IF NOT EXISTS idx_camera_acl_cam ON camera_acl(camera_id)");
            db_->exec("INSERT OR IGNORE INTO schema_version(version) VALUES(7)");
            db_->exec("RELEASE migrate_v7");
        } catch (const std::exception& e) {
            try {
                db_->exec("ROLLBACK TO migrate_v7");
                db_->exec("RELEASE migrate_v7");
            } catch (...) {}
            throw std::runtime_error(std::string("migration v7 failed: ") + e.what());
        }
    }
    if (schemaVersion() < 8) {
        try {
            db_->exec("SAVEPOINT migrate_v8");
            db_->exec(
                "CREATE TABLE IF NOT EXISTS live_layouts ("
                " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                " user_login TEXT NOT NULL,"
                " name TEXT NOT NULL,"
                " payload_json TEXT NOT NULL,"
                " updated_at TEXT NOT NULL DEFAULT (datetime('now')),"
                " UNIQUE(user_login, name))");
            db_->exec("CREATE INDEX IF NOT EXISTS idx_live_layouts_user ON live_layouts(user_login)");
            db_->exec("INSERT OR IGNORE INTO schema_version(version) VALUES(8)");
            db_->exec("RELEASE migrate_v8");
        } catch (const std::exception& e) {
            try {
                db_->exec("ROLLBACK TO migrate_v8");
                db_->exec("RELEASE migrate_v8");
            } catch (...) {}
            throw std::runtime_error(std::string("migration v8 failed: ") + e.what());
        }
    }
    NVR_INFO("db", "schema version: %d", schemaVersion());
    (void)kMigration003;  // kept as the canonical reference text for migration tools
}

void Database::purgeExpiredAuthData() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    try {
        db_->exec("DELETE FROM refresh_tokens WHERE datetime(expires_at) < datetime('now')");
        db_->exec("DELETE FROM token_revocations WHERE datetime(expires_at) < datetime('now')");
        db_->exec("DELETE FROM notify_dead_letter WHERE datetime(ts) < datetime('now', '-90 day')");
    } catch (const std::exception& e) {
        NVR_WARN("db", "purgeExpiredAuthData: %s", e.what());
    }
}

void Database::setSetting(const std::string& key, const std::string& value_json) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    SQLite::Statement q(*db_,
        "INSERT INTO settings(key, value) VALUES(?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value");
    q.bind(1, key);
    q.bind(2, value_json);
    q.exec();
}

std::string Database::getSetting(const std::string& key, const std::string& def) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    SQLite::Statement q(*db_, "SELECT value FROM settings WHERE key=?");
    q.bind(1, key);
    if (q.executeStep()) return q.getColumn(0).getString();
    return def;
}

int64_t Database::insertSegment(const std::string& camera_id, const std::string& path,
                                 const std::string& started_at, const std::string& ended_at,
                                 int64_t duration_ms, int64_t size_bytes, bool has_motion) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    SQLite::Statement q(*db_,
        "INSERT INTO segments(camera_id, path, started_at, ended_at, duration_ms, size_bytes, has_motion, finalized) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, 1)");
    q.bind(1, camera_id);
    q.bind(2, path);
    q.bind(3, started_at);
    q.bind(4, ended_at);
    q.bind(5, static_cast<int64_t>(duration_ms));
    q.bind(6, static_cast<int64_t>(size_bytes));
    q.bind(7, has_motion ? 1 : 0);
    q.exec();
    return db_->getLastInsertRowid();
}

void Database::deleteSegmentByPath(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    SQLite::Statement q(*db_, "DELETE FROM segments WHERE path=?");
    q.bind(1, path);
    q.exec();
}

int64_t Database::insertEvent(const std::string& camera_id, const std::string& type,
                               const std::string& severity, const std::string& payload_json,
                               const std::string& snapshot_path, const std::string& clip_path) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    SQLite::Statement q(*db_,
        "INSERT INTO events(camera_id, ts, type, severity, payload_json, snapshot_path, clip_path) "
        "VALUES(?, datetime('now'), ?, ?, ?, ?, ?)");
    if (camera_id.empty()) q.bind(1); else q.bind(1, camera_id);
    q.bind(2, type);
    q.bind(3, severity);
    q.bind(4, payload_json.empty() ? std::string("{}") : payload_json);
    if (snapshot_path.empty()) q.bind(5); else q.bind(5, snapshot_path);
    if (clip_path.empty()) q.bind(6); else q.bind(6, clip_path);
    q.exec();
    return db_->getLastInsertRowid();
}

bool Database::ackEvent(int64_t id, const std::string& user) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    SQLite::Statement q(*db_, "UPDATE events SET acknowledged=1 WHERE id=?");
    q.bind(1, id);
    int n = q.exec();
    if (n > 0) audit(user, "event.ack", std::to_string(id), "{}");
    return n > 0;
}

int64_t Database::insertAiDetection(const std::string& camera_id, const std::string& object_type,
                                     double confidence, int64_t track_id,
                                     const std::string& bbox_json) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    SQLite::Statement q(
        *db_,
        "INSERT INTO ai_detections(camera_id, object_type, confidence, track_id, bbox_json) "
        "VALUES(?,?,?,?,?)");
    q.bind(1, camera_id);
    q.bind(2, object_type);
    q.bind(3, confidence);
    q.bind(4, track_id);
    q.bind(5, bbox_json.empty() ? std::string("{}") : bbox_json);
    q.exec();
    return db_->getLastInsertRowid();
}

std::string Database::listLiveLayoutsJson(const std::string& user_login) {
    using json = nlohmann::json;
    std::lock_guard<std::recursive_mutex> lk(mu_);
    json arr = json::array();
    SQLite::Statement q(
        *db_, "SELECT id, name, payload_json, updated_at FROM live_layouts WHERE user_login=? ORDER BY name");
    q.bind(1, user_login);
    while (q.executeStep()) {
        const std::string pj = q.getColumn(2).getString();
        json              payload = json::parse(pj, nullptr, false);
        if (payload.is_discarded()) payload = json::object();
        arr.push_back({{"id", q.getColumn(0).getInt64()},
                       {"name", q.getColumn(1).getString()},
                       {"payload", std::move(payload)},
                       {"updated_at", q.getColumn(3).getString()}});
    }
    return arr.dump();
}

void Database::upsertLiveLayout(const std::string& user_login, const std::string& name,
                                 const std::string& payload_json) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    SQLite::Statement q(
        *db_,
        "INSERT INTO live_layouts(user_login, name, payload_json, updated_at) "
        "VALUES(?,?,?,datetime('now')) "
        "ON CONFLICT(user_login, name) DO UPDATE SET "
        "payload_json=excluded.payload_json, updated_at=excluded.updated_at");
    q.bind(1, user_login);
    q.bind(2, name);
    q.bind(3, payload_json);
    q.exec();
}

bool Database::deleteLiveLayout(const std::string& user_login, const std::string& name) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    SQLite::Statement q(*db_, "DELETE FROM live_layouts WHERE user_login=? AND name=?");
    q.bind(1, user_login);
    q.bind(2, name);
    q.exec();
    return db_->getChanges() > 0;
}

void Database::audit(const std::string& user, const std::string& action,
                      const std::string& target, const std::string& payload) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    SQLite::Statement q(*db_,
        "INSERT INTO audit_log(user_login, action, target, payload) VALUES(?,?,?,?)");
    q.bind(1, user);
    q.bind(2, action);
    q.bind(3, target);
    q.bind(4, payload);
    q.exec();
}

}
