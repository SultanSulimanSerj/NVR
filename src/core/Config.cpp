#include "nvr/Config.hpp"

#include "nvr/Logger.hpp"
#include "nvr/RecordingSchedule.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <stdexcept>

namespace nvr {

HwAccel parseHwAccel(const std::string& s) {
    std::string v = s;
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (v == "qsv")   return HwAccel::QSV;
    if (v == "vaapi") return HwAccel::VAAPI;
    if (v == "none" || v == "off" || v == "software") return HwAccel::None;
    return HwAccel::Auto;
}

const char* hwAccelToString(HwAccel hw) noexcept {
    switch (hw) {
        case HwAccel::QSV:   return "qsv";
        case HwAccel::VAAPI: return "vaapi";
        case HwAccel::None:  return "none";
        case HwAccel::Auto:  return "auto";
    }
    return "auto";
}

RecordingMode parseRecordingMode(const std::string& s) noexcept {
    std::string v = s;
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (v == "motion") return RecordingMode::Motion;
    if (v == "hybrid") return RecordingMode::Hybrid;
    return RecordingMode::Continuous;
}

const char* recordingModeToString(RecordingMode m) noexcept {
    switch (m) {
        case RecordingMode::Motion: return "motion";
        case RecordingMode::Hybrid: return "hybrid";
        case RecordingMode::Continuous:
        default: return "continuous";
    }
}

namespace {

template <typename T>
T getOr(const YAML::Node& n, const char* key, T def) {
    if (!n || !n[key]) return def;
    return n[key].as<T>();
}

}

AppConfig loadConfig(const std::string& yaml_path) {
    YAML::Node root = YAML::LoadFile(yaml_path);
    AppConfig cfg;

    cfg.logging_level = getOr<std::string>(root, "logging_level", "info");
    cfg.logging_file  = getOr<std::string>(root, "logging_file", cfg.logging_file);

    if (root["http"]) {
        const auto& h         = root["http"];
        cfg.http.bind_address = getOr<std::string>(h, "bind_address", cfg.http.bind_address);
        cfg.http.port_http    = getOr<int>(h, "port_http",  cfg.http.port_http);
        cfg.http.port_https   = getOr<int>(h, "port_https", cfg.http.port_https);
        cfg.http.enable_https = getOr<bool>(h, "enable_https", cfg.http.enable_https);
        cfg.http.cert_path    = getOr<std::string>(h, "cert_path", cfg.http.cert_path);
        cfg.http.key_path     = getOr<std::string>(h, "key_path",  cfg.http.key_path);
        cfg.http.web_root     = getOr<std::string>(h, "web_root",  cfg.http.web_root);
        cfg.http.hls_root     = getOr<std::string>(h, "hls_root",  cfg.http.hls_root);
        cfg.http.events_dir   = getOr<std::string>(h, "events_dir", cfg.http.events_dir);
        cfg.http.jwt_secret_file =
            getOr<std::string>(h, "jwt_secret_file", cfg.http.jwt_secret_file);
        cfg.http.jwt_access_ttl_seconds =
            getOr<int>(h, "jwt_access_ttl_seconds", cfg.http.jwt_access_ttl_seconds);
        cfg.http.jwt_refresh_ttl_seconds =
            getOr<int>(h, "jwt_refresh_ttl_seconds", cfg.http.jwt_refresh_ttl_seconds);
        cfg.http.http_worker_threads =
            getOr<int>(h, "http_worker_threads", cfg.http.http_worker_threads);
    }

    if (root["database"]) {
        const auto& d            = root["database"];
        cfg.database.path        = getOr<std::string>(d, "path", cfg.database.path);
        cfg.database.master_key_file =
            getOr<std::string>(d, "master_key_file", cfg.database.master_key_file);
    }

    if (root["archive"]) {
        const auto& a              = root["archive"];
        cfg.archive.root_path      = getOr<std::string>(a, "root_path", cfg.archive.root_path);
        cfg.archive.segment_minutes = getOr<int>(a, "segment_minutes", cfg.archive.segment_minutes);
        cfg.archive.target_usage_ratio =
            getOr<double>(a, "target_usage_ratio", cfg.archive.target_usage_ratio);
        cfg.archive.release_to_ratio =
            getOr<double>(a, "release_to_ratio", cfg.archive.release_to_ratio);
        cfg.archive.min_keep_minutes =
            getOr<int>(a, "min_keep_minutes", cfg.archive.min_keep_minutes);
        cfg.archive.file_prefix    = getOr<std::string>(a, "file_prefix", cfg.archive.file_prefix);
        cfg.archive.file_extension =
            getOr<std::string>(a, "file_extension", cfg.archive.file_extension);
        cfg.archive.export_watermark_text =
            getOr<std::string>(a, "export_watermark_text", cfg.archive.export_watermark_text);
        if (a["extra_archive_roots"] && a["extra_archive_roots"].IsSequence()) {
            cfg.archive.extra_archive_roots.clear();
            for (const auto& n : a["extra_archive_roots"]) {
                if (n.IsScalar()) cfg.archive.extra_archive_roots.push_back(n.as<std::string>());
            }
        }
    }

    if (root["motion"]) {
        const auto& m                = root["motion"];
        cfg.motion.downscale_width   = getOr<int>(m, "downscale_width", cfg.motion.downscale_width);
        cfg.motion.downscale_height  = getOr<int>(m, "downscale_height", cfg.motion.downscale_height);
        cfg.motion.history           = getOr<int>(m, "history", cfg.motion.history);
        cfg.motion.var_threshold     = getOr<double>(m, "var_threshold", cfg.motion.var_threshold);
        cfg.motion.detect_shadows    = getOr<bool>(m, "detect_shadows", cfg.motion.detect_shadows);
        cfg.motion.min_area_ratio    = getOr<double>(m, "min_area_ratio", cfg.motion.min_area_ratio);
        cfg.motion.cooldown_seconds  =
            getOr<int>(m, "cooldown_seconds", cfg.motion.cooldown_seconds);
        cfg.motion.snapshot_dir      = getOr<std::string>(m, "snapshot_dir", cfg.motion.snapshot_dir);
        cfg.motion.snapshot_jpeg_quality =
            getOr<int>(m, "snapshot_jpeg_quality", cfg.motion.snapshot_jpeg_quality);
    }

    if (root["python"]) {
        const auto& p              = root["python"];
        cfg.python.enabled         = getOr<bool>(p, "enabled", cfg.python.enabled);
        cfg.python.script_path     = getOr<std::string>(p, "script_path", cfg.python.script_path);
        cfg.python.callable        = getOr<std::string>(p, "callable", cfg.python.callable);
        cfg.python.include_frame   = getOr<bool>(p, "include_frame", cfg.python.include_frame);
        cfg.python.queue_capacity  =
            static_cast<size_t>(getOr<int>(p, "queue_capacity",
                                            static_cast<int>(cfg.python.queue_capacity)));
    }

    if (root["license"]) {
        const auto& L = root["license"];
        cfg.license_paths.file =
            getOr<std::string>(L, "file", cfg.license_paths.file);
        cfg.license_paths.public_key =
            getOr<std::string>(L, "public_key", cfg.license_paths.public_key);
        cfg.license_paths.trial_max_channels =
            getOr<int>(L, "trial_max_channels", cfg.license_paths.trial_max_channels);
    }

    if (!root["cameras"] || !root["cameras"].IsSequence()) {
        throw std::runtime_error("config: 'cameras' must be a non-empty sequence");
    }

    for (const auto& cn : root["cameras"]) {
        CameraConfig c;
        c.id                = getOr<std::string>(cn, "id", "");
        c.name              = getOr<std::string>(cn, "name", c.id);
        c.rtsp_url          = getOr<std::string>(cn, "rtsp_url", "");
        c.sub_rtsp_url      = getOr<std::string>(cn, "sub_rtsp_url", "");
        c.preferred_hw      = parseHwAccel(getOr<std::string>(cn, "preferred_hw", "auto"));
        c.analysis_fps      = getOr<int>(cn, "analysis_fps", c.analysis_fps);
        c.enable_motion     = getOr<bool>(cn, "enable_motion", c.enable_motion);
        c.enable_recording  = getOr<bool>(cn, "enable_recording", c.enable_recording);
        c.recording_mode    = parseRecordingMode(getOr<std::string>(cn, "recording_mode", "continuous"));
        c.enable_substream  = getOr<bool>(cn, "enable_substream", c.enable_substream);

        c.onvif_host        = getOr<std::string>(cn, "onvif_host", "");
        c.onvif_port        = getOr<int>(cn, "onvif_port", c.onvif_port);
        c.onvif_user        = getOr<std::string>(cn, "onvif_user", "");
        c.onvif_pass        = getOr<std::string>(cn, "onvif_pass", "");

        c.pre_event_seconds  = getOr<int>(cn, "pre_event_seconds",  c.pre_event_seconds);
        c.post_event_seconds = getOr<int>(cn, "post_event_seconds", c.post_event_seconds);
        c.sub_bitrate_kbps   = getOr<int>(cn, "sub_bitrate_kbps",   c.sub_bitrate_kbps);
        c.sub_width          = getOr<int>(cn, "sub_width",          c.sub_width);
        c.sub_height         = getOr<int>(cn, "sub_height",         c.sub_height);
        c.sub_fps            = getOr<int>(cn, "sub_fps",            c.sub_fps);

        c.recording_schedule_json =
            getOr<std::string>(cn, "recording_schedule_json", c.recording_schedule_json);
        if (auto n = tryNormalizeRecordingScheduleJson(c.recording_schedule_json))
            c.recording_schedule_json = *n;
        else
            throw std::runtime_error("config: invalid recording_schedule_json for camera " + c.id);

        c.plan_x = getOr<double>(cn, "plan_x", c.plan_x);
        c.plan_y = getOr<double>(cn, "plan_y", c.plan_y);

        if (c.id.empty() || c.rtsp_url.empty()) {
            throw std::runtime_error("config: each camera must have 'id' and 'rtsp_url'");
        }
        cfg.cameras.push_back(std::move(c));
    }

    if (cfg.cameras.empty()) {
        throw std::runtime_error("config: at least one camera must be configured");
    }

    return cfg;
}

}
