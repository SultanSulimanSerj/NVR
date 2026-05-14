#include "nvr/store/ConfigStore.hpp"

#include "nvr/Logger.hpp"

#include <nlohmann/json.hpp>

namespace nvr::store {

namespace {

std::string roisToJson(const std::vector<MotionRoi>& rois) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& r : rois) {
        nlohmann::json poly = nlohmann::json::array();
        for (const auto& [x, y] : r.polygon_norm) poly.push_back({x, y});
        out.push_back({{"name", r.name}, {"polygon", poly}});
    }
    return out.dump();
}

std::vector<MotionRoi> jsonToRois(const std::string& j) {
    std::vector<MotionRoi> out;
    try {
        auto root = nlohmann::json::parse(j);
        for (const auto& r : root) {
            MotionRoi roi;
            roi.name = r.value("name", std::string{});
            for (const auto& p : r.value("polygon", nlohmann::json::array())) {
                if (p.is_array() && p.size() == 2) {
                    roi.polygon_norm.emplace_back(p[0].get<double>(), p[1].get<double>());
                }
            }
            out.push_back(std::move(roi));
        }
    } catch (...) {}
    return out;
}

}

ConfigStore::ConfigStore(Database& db, MasterKey k) : db_(db), key_(std::move(k)) {}

CameraConfig ConfigStore::rowToCamera(SQLite::Statement& q) {
    CameraConfig c;
    c.id                  = q.getColumn("id").getString();
    c.name                = q.getColumn("name").getString();
    c.rtsp_url            = q.getColumn("rtsp_url").getString();
    c.sub_rtsp_url        = q.getColumn("sub_rtsp_url").getString();
    c.preferred_hw        = parseHwAccel(q.getColumn("preferred_hw").getString());
    c.analysis_fps        = q.getColumn("analysis_fps").getInt();
    c.enable_motion       = q.getColumn("enable_motion").getInt() != 0;
    c.enable_recording    = q.getColumn("enable_recording").getInt() != 0;
    c.recording_mode      = parseRecordingMode(q.getColumn("recording_mode").getString());
    c.enable_substream    = q.getColumn("enable_substream").getInt() != 0;
    c.onvif_host          = q.getColumn("onvif_host").getString();
    c.onvif_port          = q.getColumn("onvif_port").getInt();
    c.onvif_user          = q.getColumn("onvif_user").getString();

    auto pass_col = q.getColumn("onvif_pass_enc");
    if (!pass_col.isNull()) {
        const void* raw = pass_col.getBlob();
        std::vector<uint8_t> blob(static_cast<const uint8_t*>(raw),
                                   static_cast<const uint8_t*>(raw) + pass_col.getBytes());
        if (auto dec = decrypt(key_, blob, "onvif_pass:" + c.id)) c.onvif_pass = *dec;
    }
    c.motion_rois         = jsonToRois(q.getColumn("motion_roi_json").getString());
    c.pre_event_seconds   = q.getColumn("pre_event_seconds").getInt();
    c.post_event_seconds  = q.getColumn("post_event_seconds").getInt();
    c.sub_bitrate_kbps    = q.getColumn("sub_bitrate_kbps").getInt();
    c.sub_width           = q.getColumn("sub_width").getInt();
    c.sub_height          = q.getColumn("sub_height").getInt();
    c.sub_fps             = q.getColumn("sub_fps").getInt();
    c.recording_schedule_json = q.getColumn("recording_schedule_json").getString();
    c.plan_x                  = q.getColumn("plan_x").getDouble();
    c.plan_y                  = q.getColumn("plan_y").getDouble();
    return c;
}

std::vector<CameraConfig> ConfigStore::listCameras() {
    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    SQLite::Statement q(db_.raw(), "SELECT * FROM cameras WHERE disabled=0 ORDER BY id");
    std::vector<CameraConfig> out;
    while (q.executeStep()) out.push_back(rowToCamera(q));
    return out;
}

std::optional<CameraConfig> ConfigStore::getCamera(const std::string& id) {
    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    SQLite::Statement q(db_.raw(), "SELECT * FROM cameras WHERE id=?");
    q.bind(1, id);
    if (!q.executeStep()) return std::nullopt;
    return rowToCamera(q);
}

void ConfigStore::upsertCamera(const CameraConfig& c, const std::string& actor) {
    std::lock_guard<std::recursive_mutex> lk(db_.mutex());

    auto existing = getCamera(c.id);

    std::vector<uint8_t> pass_enc;
    if (!c.onvif_pass.empty()) {
        pass_enc = encrypt(key_, c.onvif_pass, "onvif_pass:" + c.id);
    } else if (existing) {
        // PATCH that doesn't carry onvif_pass must not erase the stored secret.
        // Read the existing raw blob directly so we re-bind it verbatim.
        SQLite::Statement pq(db_.raw(), "SELECT onvif_pass_enc FROM cameras WHERE id=?");
        pq.bind(1, c.id);
        if (pq.executeStep() && !pq.getColumn(0).isNull()) {
            auto col = pq.getColumn(0);
            const auto* raw = static_cast<const uint8_t*>(col.getBlob());
            pass_enc.assign(raw, raw + col.getBytes());
        }
    }

    SQLite::Statement q(db_.raw(), R"SQL(
        INSERT INTO cameras(
            id, name, rtsp_url, sub_rtsp_url, preferred_hw, analysis_fps,
            enable_motion, enable_recording, recording_mode, enable_substream,
            onvif_host, onvif_port, onvif_user, onvif_pass_enc,
            motion_roi_json, pre_event_seconds, post_event_seconds,
            sub_bitrate_kbps, sub_width, sub_height, sub_fps,
            recording_schedule_json, plan_x, plan_y, updated_at)
        VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,datetime('now'))
        ON CONFLICT(id) DO UPDATE SET
            name=excluded.name, rtsp_url=excluded.rtsp_url,
            sub_rtsp_url=excluded.sub_rtsp_url, preferred_hw=excluded.preferred_hw,
            analysis_fps=excluded.analysis_fps, enable_motion=excluded.enable_motion,
            enable_recording=excluded.enable_recording, recording_mode=excluded.recording_mode,
            enable_substream=excluded.enable_substream,
            onvif_host=excluded.onvif_host, onvif_port=excluded.onvif_port,
            onvif_user=excluded.onvif_user, onvif_pass_enc=excluded.onvif_pass_enc,
            motion_roi_json=excluded.motion_roi_json,
            pre_event_seconds=excluded.pre_event_seconds,
            post_event_seconds=excluded.post_event_seconds,
            sub_bitrate_kbps=excluded.sub_bitrate_kbps,
            sub_width=excluded.sub_width, sub_height=excluded.sub_height, sub_fps=excluded.sub_fps,
            recording_schedule_json=excluded.recording_schedule_json,
            plan_x=excluded.plan_x, plan_y=excluded.plan_y,
            updated_at=datetime('now')
    )SQL");
    q.bind(1, c.id);
    q.bind(2, c.name);
    q.bind(3, c.rtsp_url);
    q.bind(4, c.sub_rtsp_url);
    q.bind(5, hwAccelToString(c.preferred_hw));
    q.bind(6, c.analysis_fps);
    q.bind(7, c.enable_motion ? 1 : 0);
    q.bind(8, c.enable_recording ? 1 : 0);
    q.bind(9, recordingModeToString(c.recording_mode));
    q.bind(10, c.enable_substream ? 1 : 0);
    q.bind(11, c.onvif_host);
    q.bind(12, c.onvif_port);
    q.bind(13, c.onvif_user);
    if (!pass_enc.empty()) q.bind(14, pass_enc.data(), static_cast<int>(pass_enc.size()));
    else                   q.bind(14);
    q.bind(15, roisToJson(c.motion_rois));
    q.bind(16, c.pre_event_seconds);
    q.bind(17, c.post_event_seconds);
    q.bind(18, c.sub_bitrate_kbps);
    q.bind(19, c.sub_width);
    q.bind(20, c.sub_height);
    q.bind(21, c.sub_fps);
    q.bind(22, c.recording_schedule_json);
    q.bind(23, c.plan_x);
    q.bind(24, c.plan_y);
    q.exec();

    db_.audit(actor, existing ? "camera.update" : "camera.add", c.id, "{}");
    notify({existing ? CameraChangeKind::Updated : CameraChangeKind::Added, c});
}

void ConfigStore::deleteCamera(const std::string& id, const std::string& actor) {
    auto existing = getCamera(id);
    if (!existing) return;

    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    SQLite::Statement q(db_.raw(), "DELETE FROM cameras WHERE id=?");
    q.bind(1, id);
    q.exec();

    db_.audit(actor, "camera.delete", id, "{}");
    notify({CameraChangeKind::Removed, *existing});
}

ArchiveConfig ConfigStore::archiveConfig() {
    ArchiveConfig cfg;
    try {
        auto j = nlohmann::json::parse(db_.getSetting("archive", "{}"));
        cfg.root_path           = j.value("root_path", cfg.root_path);
        cfg.segment_minutes     = j.value("segment_minutes", cfg.segment_minutes);
        cfg.target_usage_ratio  = j.value("target_usage_ratio", cfg.target_usage_ratio);
        cfg.release_to_ratio    = j.value("release_to_ratio", cfg.release_to_ratio);
        cfg.min_keep_minutes    = j.value("min_keep_minutes", cfg.min_keep_minutes);
        cfg.file_prefix         = j.value("file_prefix", cfg.file_prefix);
        cfg.file_extension      = j.value("file_extension", cfg.file_extension);
        cfg.export_watermark_text = j.value("export_watermark_text", cfg.export_watermark_text);
        if (j.contains("extra_archive_roots") && j["extra_archive_roots"].is_array()) {
            cfg.extra_archive_roots.clear();
            for (const auto& e : j["extra_archive_roots"]) {
                if (e.is_string()) cfg.extra_archive_roots.push_back(e.get<std::string>());
            }
        }
    } catch (...) {}
    return cfg;
}

void ConfigStore::setArchiveConfig(const ArchiveConfig& cfg) {
    nlohmann::json j = {
        {"root_path",          cfg.root_path},
        {"segment_minutes",    cfg.segment_minutes},
        {"target_usage_ratio", cfg.target_usage_ratio},
        {"release_to_ratio",   cfg.release_to_ratio},
        {"min_keep_minutes",   cfg.min_keep_minutes},
        {"file_prefix",        cfg.file_prefix},
        {"file_extension",     cfg.file_extension},
        {"export_watermark_text", cfg.export_watermark_text},
        {"extra_archive_roots",   cfg.extra_archive_roots},
    };
    db_.setSetting("archive", j.dump());
}

MotionConfig ConfigStore::motionConfig() {
    MotionConfig cfg;
    try {
        auto j = nlohmann::json::parse(db_.getSetting("motion", "{}"));
        cfg.downscale_width      = j.value("downscale_width", cfg.downscale_width);
        cfg.downscale_height     = j.value("downscale_height", cfg.downscale_height);
        cfg.history              = j.value("history", cfg.history);
        cfg.var_threshold        = j.value("var_threshold", cfg.var_threshold);
        cfg.detect_shadows       = j.value("detect_shadows", cfg.detect_shadows);
        cfg.min_area_ratio       = j.value("min_area_ratio", cfg.min_area_ratio);
        cfg.cooldown_seconds     = j.value("cooldown_seconds", cfg.cooldown_seconds);
        cfg.snapshot_dir         = j.value("snapshot_dir", cfg.snapshot_dir);
        cfg.snapshot_jpeg_quality= j.value("snapshot_jpeg_quality", cfg.snapshot_jpeg_quality);
    } catch (...) {}
    return cfg;
}

void ConfigStore::setMotionConfig(const MotionConfig& cfg) {
    nlohmann::json j = {
        {"downscale_width",       cfg.downscale_width},
        {"downscale_height",      cfg.downscale_height},
        {"history",               cfg.history},
        {"var_threshold",         cfg.var_threshold},
        {"detect_shadows",        cfg.detect_shadows},
        {"min_area_ratio",        cfg.min_area_ratio},
        {"cooldown_seconds",      cfg.cooldown_seconds},
        {"snapshot_dir",          cfg.snapshot_dir},
        {"snapshot_jpeg_quality", cfg.snapshot_jpeg_quality},
    };
    db_.setSetting("motion", j.dump());
}

void ConfigStore::importFromYaml(const AppConfig& yaml_cfg) {
    auto existing = listCameras();
    setArchiveConfig(yaml_cfg.archive);
    setMotionConfig(yaml_cfg.motion);
    if (existing.empty()) {
        for (const auto& c : yaml_cfg.cameras) upsertCamera(c, "yaml-import");
        NVR_INFO("config-store", "imported %zu cameras from YAML", yaml_cfg.cameras.size());
    }
}

void ConfigStore::addCameraListener(CameraListener cb) {
    std::lock_guard<std::mutex> lk(listeners_mu_);
    listeners_.push_back(std::move(cb));
}

void ConfigStore::notify(const CameraChange& ch) {
    std::vector<CameraListener> snapshot;
    {
        std::lock_guard<std::mutex> lk(listeners_mu_);
        snapshot = listeners_;
    }
    for (auto& cb : snapshot) {
        try { cb(ch); }
        catch (const std::exception& e) {
            NVR_WARN("config-store", "listener exception: %s", e.what());
        }
    }
}

}
