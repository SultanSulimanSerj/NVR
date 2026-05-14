#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace nvr {

enum class HwAccel { None, QSV, VAAPI, Auto };

/// When `enable_recording` is true: continuous mux vs motion-window mux (pre/post seconds).
/// `Hybrid` = continuous mux (schedule) + motion marks segments / events (`enable_motion` required).
enum class RecordingMode { Continuous, Motion, Hybrid };

RecordingMode parseRecordingMode(const std::string& s) noexcept;
const char*   recordingModeToString(RecordingMode m) noexcept;

struct MotionRoi {
    std::string name;
    std::vector<std::pair<double, double>> polygon_norm;
};

struct CameraConfig {
    std::string id;
    std::string name;
    std::string rtsp_url;
    std::string sub_rtsp_url;
    HwAccel     preferred_hw{HwAccel::Auto};
    int         analysis_fps{6};
    bool        enable_motion{true};
    bool        enable_recording{true};
    RecordingMode recording_mode{RecordingMode::Continuous};
    bool        enable_substream{true};

    std::string onvif_host;
    int         onvif_port{80};
    std::string onvif_user;
    std::string onvif_pass;

    std::vector<MotionRoi> motion_rois;
    int         pre_event_seconds{5};
    int         post_event_seconds{10};

    int         sub_bitrate_kbps{512};
    int         sub_width{640};
    int         sub_height{360};
    int         sub_fps{15};

    /// Weekly recording windows (JSON). Validated on API write; see `RecordingSchedule.hpp`.
    std::string recording_schedule_json{R"({"always":true})"};
    /// Normalized 0..1 position on facility map; negative values mean unset.
    double      plan_x{-1.0};
    double      plan_y{-1.0};
};

struct ArchiveConfig {
    std::string root_path{"/var/lib/nvr-prototype/archive"};
    /// Additional archive roots (new segments round-robin to freest disk; retention scans all).
    std::vector<std::string> extra_archive_roots;
    int         segment_minutes{60};
    double      target_usage_ratio{0.80};
    double      release_to_ratio{0.78};
    int         min_keep_minutes{15};
    std::string file_prefix{"cam_"};
    std::string file_extension{".mp4"};
    /// If non-empty, export endpoints may burn text overlay (see RouteArchiveEvents).
    std::string export_watermark_text;
};

struct MotionConfig {
    int    downscale_width{640};
    int    downscale_height{360};
    int    history{300};
    double var_threshold{16.0};
    bool   detect_shadows{false};
    double min_area_ratio{0.005};
    int    cooldown_seconds{5};
    std::string snapshot_dir{"/var/lib/nvr-prototype/snapshots"};
    int    snapshot_jpeg_quality{85};
};

struct PythonConfig {
    bool        enabled{true};
    std::string script_path{"/usr/share/nvr-prototype/scripts/script.py"};
    std::string callable{"on_motion"};
    bool        include_frame{false};
    size_t      queue_capacity{128};
};

struct HttpConfig {
    // Default to loopback: TLS terminates at nginx in front of us, and the
    // operator opts in to a real listen address by editing config.yaml.
    std::string bind_address{"127.0.0.1"};
    int         port_http{8080};
    // Legacy: unused at runtime. TLS terminates on nginx (default 443); the
    // Crow daemon listens on port_http (loopback). Kept for backwards-compatible YAML.
    int         port_https{8443};
    bool        enable_https{false};
    std::string cert_path{"/etc/nvr-prototype/tls/cert.pem"};
    std::string key_path{"/etc/nvr-prototype/tls/key.pem"};
    std::string web_root{"/usr/share/nvr-prototype/webui"};
    std::string hls_root{"/var/lib/nvr-prototype/hls"};
    std::string events_dir{"/var/lib/nvr-prototype/events"};
    std::string jwt_secret_file{"/etc/nvr-prototype/jwt.secret"};
    /// Access JWT lifetime (interactive UI / API). Refresh lifetime is separate.
    int jwt_access_ttl_seconds{900};
    int jwt_refresh_ttl_seconds{43200};
    /// Crow worker threads (minimum 2 enforced at runtime). 0 = hardware_concurrency().
    int http_worker_threads{0};
};

struct DatabaseConfig {
    std::string path{"/var/lib/nvr-prototype/nvr.db"};
    std::string master_key_file{"/etc/nvr-prototype/master.key"};
};

/// Paths and trial limits for offline Ed25519 license (see docs/LICENSE_TICKET.md).
struct LicensePaths {
    std::string file{"/etc/nvr-prototype/license.lic"};
    std::string public_key{"/etc/nvr-prototype/license.pub"};
    int         trial_max_channels{8};
};

struct AppConfig {
    std::string                logging_level{"info"};
    std::string                logging_file{"/var/log/nvr-prototype/nvr.log"};
    std::vector<CameraConfig>  cameras;
    ArchiveConfig              archive;
    MotionConfig               motion;
    PythonConfig               python;
    HttpConfig                 http;
    DatabaseConfig             database;
    LicensePaths               license_paths;
};

AppConfig loadConfig(const std::string& yaml_path);
HwAccel   parseHwAccel(const std::string& s);
const char* hwAccelToString(HwAccel hw) noexcept;
RecordingMode parseRecordingMode(const std::string& s) noexcept;
const char*   recordingModeToString(RecordingMode m) noexcept;

}
