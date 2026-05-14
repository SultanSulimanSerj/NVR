CREATE TABLE IF NOT EXISTS schema_version (
    version INTEGER PRIMARY KEY
);

CREATE TABLE IF NOT EXISTS cameras (
    id                  TEXT    PRIMARY KEY,
    name                TEXT    NOT NULL,
    rtsp_url            TEXT    NOT NULL,
    sub_rtsp_url        TEXT    DEFAULT '',
    preferred_hw        TEXT    DEFAULT 'auto',
    analysis_fps        INTEGER DEFAULT 6,
    enable_motion       INTEGER DEFAULT 1,
    enable_recording    INTEGER DEFAULT 1,
    enable_substream    INTEGER DEFAULT 1,
    onvif_host          TEXT    DEFAULT '',
    onvif_port          INTEGER DEFAULT 80,
    onvif_user          TEXT    DEFAULT '',
    onvif_pass_enc      BLOB,
    motion_roi_json     TEXT    DEFAULT '[]',
    pre_event_seconds   INTEGER DEFAULT 5,
    post_event_seconds  INTEGER DEFAULT 10,
    sub_bitrate_kbps    INTEGER DEFAULT 512,
    sub_width           INTEGER DEFAULT 640,
    sub_height          INTEGER DEFAULT 360,
    sub_fps             INTEGER DEFAULT 15,
    disabled            INTEGER DEFAULT 0,
    created_at          TEXT    NOT NULL DEFAULT (datetime('now')),
    updated_at          TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS segments (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    camera_id    TEXT    NOT NULL,
    path         TEXT    NOT NULL,
    started_at   TEXT    NOT NULL,
    ended_at     TEXT    NOT NULL,
    duration_ms  INTEGER NOT NULL,
    size_bytes   INTEGER NOT NULL,
    has_motion   INTEGER DEFAULT 0,
    FOREIGN KEY(camera_id) REFERENCES cameras(id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_segments_cam_start ON segments(camera_id, started_at);

CREATE TABLE IF NOT EXISTS events (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    camera_id     TEXT,
    ts            TEXT    NOT NULL,
    type          TEXT    NOT NULL,
    severity      TEXT    NOT NULL DEFAULT 'info',
    payload_json  TEXT    DEFAULT '{}',
    snapshot_path TEXT,
    clip_path     TEXT,
    acknowledged  INTEGER DEFAULT 0,
    dispatched    INTEGER DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_events_ts        ON events(ts);
CREATE INDEX IF NOT EXISTS idx_events_camera_ts ON events(camera_id, ts);
CREATE INDEX IF NOT EXISTS idx_events_type      ON events(type);

CREATE TABLE IF NOT EXISTS users (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    login           TEXT    UNIQUE NOT NULL,
    password_hash   TEXT    NOT NULL,
    role            TEXT    NOT NULL DEFAULT 'viewer',
    totp_secret_enc BLOB,
    disabled        INTEGER DEFAULT 0,
    created_at      TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS settings (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS audit_log (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ts          TEXT NOT NULL DEFAULT (datetime('now')),
    user_login  TEXT,
    action      TEXT NOT NULL,
    target      TEXT,
    payload     TEXT
);

CREATE TABLE IF NOT EXISTS notification_channels (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    kind        TEXT NOT NULL,
    name        TEXT NOT NULL,
    config_enc  BLOB,
    enabled     INTEGER DEFAULT 1
);

CREATE TABLE IF NOT EXISTS notification_rules (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    camera_id         TEXT,
    event_type        TEXT NOT NULL,
    severity_min      TEXT NOT NULL DEFAULT 'info',
    throttle_seconds  INTEGER NOT NULL DEFAULT 30,
    channel_id        INTEGER NOT NULL,
    FOREIGN KEY(channel_id) REFERENCES notification_channels(id) ON DELETE CASCADE
);

INSERT OR IGNORE INTO schema_version(version) VALUES(1);
