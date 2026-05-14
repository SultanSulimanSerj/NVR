#include "nvr/api/HttpRouteHelpers.hpp"

#include "nvr/RecordingSchedule.hpp"
#include "nvr/store/ConfigStore.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <spawn.h>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace nvr::api {

namespace {

constexpr const char* kBearer = "Bearer ";

} // namespace

std::optional<std::string> extractAuthToken(const crow::request& req) {
    auto auth = req.get_header_value("Authorization");
    if (auth.rfind(kBearer, 0) == 0) return auth.substr(std::strlen(kBearer));
    auto cookies = req.get_header_value("Cookie");
    constexpr std::string_view kCookieName = "nvr_token=";
    auto pos = cookies.find(kCookieName);
    if (pos != std::string::npos) {
        pos += kCookieName.size();
        auto end = cookies.find(';', pos);
        return cookies.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    }
    return std::nullopt;
}

std::optional<std::string> extractStreamToken(const crow::request& req) {
    auto auth = extractAuthToken(req);
    if (auth) return auth;
    if (auto qit = req.url_params.get("token")) return std::string(qit);
    return std::nullopt;
}

bool jsonRefreshTokenOptIn(const crow::request& req) {
    auto h = req.get_header_value("Accept");
    return h.find("application/vnd.nvr.auth+json") != std::string::npos;
}

std::string clientIp(const crow::request& req) {
    auto xff = req.get_header_value("X-Forwarded-For");
    if (!xff.empty()) {
        auto c = xff.find(',');
        return c == std::string::npos ? xff : xff.substr(0, c);
    }
    return req.remote_ip_address;
}

bool roleAtLeast(Role have, Role required) {
    auto rank = [](Role r) {
        switch (r) {
            case Role::Admin: return 3;
            case Role::Operator: return 2;
            case Role::Viewer: return 1;
        }
        return 0;
    };
    return rank(have) >= rank(required);
}

std::string isoNow() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string mimeFor(const fs::path& p) {
    auto ext = p.extension().string();
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css";
    if (ext == ".js" || ext == ".mjs") return "application/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".mp4") return "video/mp4";
    if (ext == ".m4s") return "video/iso.segment";
    if (ext == ".m3u8") return "application/vnd.apple.mpegurl";
    return "application/octet-stream";
}

bool readFile(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::string cameraToJson(const CameraConfig& c) {
    json roi = json::array();
    for (const auto& r : c.motion_rois) {
        json poly = json::array();
        for (auto& [x, y] : r.polygon_norm) poly.push_back({x, y});
        roi.push_back({{"name", r.name}, {"polygon", poly}});
    }
    json j = {
        {"id", c.id}, {"name", c.name},
        {"rtsp_url", c.rtsp_url}, {"sub_rtsp_url", c.sub_rtsp_url},
        {"preferred_hw", hwAccelToString(c.preferred_hw)},
        {"analysis_fps", c.analysis_fps},
        {"enable_motion", c.enable_motion},
        {"enable_recording", c.enable_recording},
        {"recording_mode", recordingModeToString(c.recording_mode)},
        {"enable_substream", c.enable_substream},
        {"onvif_host", c.onvif_host}, {"onvif_port", c.onvif_port},
        {"onvif_user", c.onvif_user},
        {"pre_event_seconds", c.pre_event_seconds},
        {"post_event_seconds", c.post_event_seconds},
        {"sub_bitrate_kbps", c.sub_bitrate_kbps},
        {"sub_width", c.sub_width}, {"sub_height", c.sub_height}, {"sub_fps", c.sub_fps},
        {"motion_rois", roi},
        {"plan_x", c.plan_x},
        {"plan_y", c.plan_y},
    };
    try {
        j["recording_schedule"] = json::parse(c.recording_schedule_json);
    } catch (...) {
        j["recording_schedule"] = json::object({{"always", true}});
    }
    return j.dump();
}

CameraConfig jsonToCamera(const json& j, const std::string& id_override) {
    CameraConfig c;
    c.id               = id_override.empty() ? j.value("id", "") : id_override;
    c.name             = j.value("name", c.id);
    c.rtsp_url         = j.value("rtsp_url", "");
    c.sub_rtsp_url     = j.value("sub_rtsp_url", "");
    c.preferred_hw     = parseHwAccel(j.value("preferred_hw", "auto"));
    c.analysis_fps     = j.value("analysis_fps", c.analysis_fps);
    c.enable_motion    = j.value("enable_motion", c.enable_motion);
    c.enable_recording = j.value("enable_recording", c.enable_recording);
    c.recording_mode   = parseRecordingMode(j.value("recording_mode", std::string("continuous")));
    c.enable_substream = j.value("enable_substream", c.enable_substream);
    c.onvif_host       = j.value("onvif_host", "");
    c.onvif_port       = j.value("onvif_port", c.onvif_port);
    c.onvif_user       = j.value("onvif_user", "");
    c.onvif_pass       = j.value("onvif_pass", "");
    c.pre_event_seconds  = j.value("pre_event_seconds", c.pre_event_seconds);
    c.post_event_seconds = j.value("post_event_seconds", c.post_event_seconds);
    c.sub_bitrate_kbps   = j.value("sub_bitrate_kbps", c.sub_bitrate_kbps);
    c.sub_width          = j.value("sub_width", c.sub_width);
    c.sub_height         = j.value("sub_height", c.sub_height);
    c.sub_fps            = j.value("sub_fps", c.sub_fps);
    if (j.contains("motion_rois")) {
        for (const auto& r : j["motion_rois"]) {
            MotionRoi roi;
            roi.name = r.value("name", "");
            for (const auto& p : r.value("polygon", json::array())) {
                if (p.is_array() && p.size() == 2) {
                    roi.polygon_norm.emplace_back(p[0].get<double>(), p[1].get<double>());
                }
            }
            c.motion_rois.push_back(std::move(roi));
        }
    }
    if (j.contains("recording_schedule")) {
        if (!j["recording_schedule"].is_object())
            throw std::runtime_error("bad_recording_schedule");
        c.recording_schedule_json =
            nvr::normalizeRecordingScheduleJsonOrThrow(j["recording_schedule"].dump());
    }
    c.plan_x = j.value("plan_x", c.plan_x);
    c.plan_y = j.value("plan_y", c.plan_y);
    return c;
}

bool isSafeToken(const std::string& s, size_t max_len) {
    if (s.empty() || s.size() > max_len) return false;
    for (char ch : s) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == '.'))
            return false;
    }
    return true;
}

bool isSafeHostname(const std::string& s) {
    if (s.empty() || s.size() > 253) return false;
    for (char ch : s) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '.')) return false;
    }
    return s.front() != '-' && s.back() != '-' && s.front() != '.';
}

bool isSafeTimezone(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char ch : s) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == '+' ||
              ch == '/'))
            return false;
    }
    return s.find("..") == std::string::npos;
}

bool isSafeBlockDevice(const std::string& s) {
    if (s.empty() || s.size() > 32) return false;
    for (char ch : s) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_')) return false;
    }
    return s.find("..") == std::string::npos;
}

std::string runArgv(const std::vector<std::string>& argv, int max_bytes) {
    if (argv.empty()) return {};
    int pipefd[2];
    if (::pipe(pipefd) != 0) return {};

    posix_spawn_file_actions_t fa;
    if (posix_spawn_file_actions_init(&fa) != 0) {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        return {};
    }
    posix_spawn_file_actions_addclose(&fa, pipefd[0]);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&fa, pipefd[1]);

    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (auto& s : argv) cargv.push_back(const_cast<char*>(s.c_str()));
    cargv.push_back(nullptr);

    pid_t pid = 0;
    int rc = posix_spawnp(&pid, cargv[0], &fa, nullptr, cargv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    ::close(pipefd[1]);
    if (rc != 0) {
        ::close(pipefd[0]);
        return {};
    }

    std::string out;
    std::array<char, 4096> buf{};
    while (true) {
        auto n = ::read(pipefd[0], buf.data(), buf.size());
        if (n <= 0) break;
        if (static_cast<int>(out.size() + n) > max_bytes) break;
        out.append(buf.data(), static_cast<size_t>(n));
    }
    ::close(pipefd[0]);
    int status = 0;
    ::waitpid(pid, &status, 0);
    return out;
}

bool runArgvDetached(const std::vector<std::string>& argv) {
    if (argv.empty()) return false;
    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (auto& s : argv) cargv.push_back(const_cast<char*>(s.c_str()));
    cargv.push_back(nullptr);
    pid_t pid = 0;
    int rc = posix_spawnp(&pid, cargv[0], nullptr, nullptr, cargv.data(), environ);
    return rc == 0;
}

bool cameraVisibleToUser(const store::ConfigStore& store, const Identity& who,
                         const std::string& camera_id) {
    if (who.role == Role::Admin) return true;
    std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
    SQLite::Statement c(store.db().raw(), "SELECT COUNT(*) FROM camera_acl WHERE user_login=?");
    c.bind(1, who.login);
    if (!c.executeStep()) return true;
    const int total = c.getColumn(0).getInt();
    if (total == 0) return true;
    SQLite::Statement ok(store.db().raw(),
                           "SELECT 1 FROM camera_acl WHERE user_login=? AND camera_id=? LIMIT 1");
    ok.bind(1, who.login);
    ok.bind(2, camera_id);
    return ok.executeStep();
}

} // namespace nvr::api
