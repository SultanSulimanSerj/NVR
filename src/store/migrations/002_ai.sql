CREATE TABLE IF NOT EXISTS face_persons (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    name         TEXT NOT NULL,
    embedding    BLOB,
    created_at   TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS ai_models (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    kind       TEXT NOT NULL,
    path       TEXT NOT NULL,
    precision  TEXT DEFAULT 'FP16',
    device     TEXT DEFAULT 'CPU',
    enabled    INTEGER DEFAULT 1
);

CREATE TABLE IF NOT EXISTS ai_detections (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id    INTEGER,
    camera_id   TEXT NOT NULL,
    ts          TEXT NOT NULL DEFAULT (datetime('now')),
    object_type TEXT NOT NULL,
    confidence  REAL,
    track_id    INTEGER,
    bbox_json   TEXT,
    extra_json  TEXT
);
CREATE INDEX IF NOT EXISTS idx_ai_camera_ts ON ai_detections(camera_id, ts);
CREATE INDEX IF NOT EXISTS idx_ai_object    ON ai_detections(object_type);

UPDATE schema_version SET version = 2;
INSERT OR IGNORE INTO schema_version(version) VALUES(2);
