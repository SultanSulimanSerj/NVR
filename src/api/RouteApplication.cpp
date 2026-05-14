#include "nvr/api/RouteApplication.hpp"

#include "nvr/api/HttpCommon.hpp"
#include "nvr/api/HttpRouteHelpers.hpp"
#include "nvr/onvif/EnumerateStreams.hpp"
#include "nvr/onvif/SoapClient.hpp"
#include "nvr/onvif/WsDiscovery.hpp"
#include "nvr/obs/Metrics.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <crow/app.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <set>
#include <sstream>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace nvr::api {

namespace {

constexpr const char* kSetupMarker = "/var/lib/nvr-prototype/.setup-done";
constexpr const char* kSetupToken  = "/etc/nvr-prototype/setup.token";

const std::set<std::string> kAllowedServices{"nvr-prototype", "nvr-kiosk", "nginx", "chrony"};
const std::set<std::string> kAllowedServiceActions{"start", "stop", "restart", "status"};
const std::set<std::string> kAllowedPower{"reboot", "shutdown"};

void trimInPlace(std::string& s) {
    auto notsp = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notsp));
    s.erase(std::find_if(s.rbegin(), s.rend(), notsp).base(), s.end());
}

std::string resolveOnvifDeviceServiceUrl(const json& body) {
    std::string device_url = body.value("device_url", "");
    trimInPlace(device_url);
    if (!device_url.empty()) {
        if (device_url.size() > 768) throw std::runtime_error("device_url_too_long");
        return device_url;
    }
    std::string xaddrs = body.value("xaddrs", "");
    trimInPlace(xaddrs);
    if (!xaddrs.empty()) {
        std::istringstream iss(xaddrs);
        std::string        tok;
        while (iss >> tok) {
            trimInPlace(tok);
            if (tok.rfind("http://", 0) == 0 || tok.rfind("https://", 0) == 0) {
                if (tok.size() > 768) throw std::runtime_error("device_url_too_long");
                return tok;
            }
        }
    }
    std::string host = body.value("host", "");
    trimInPlace(host);
    if (host.empty()) throw std::runtime_error("host_or_device_url_required");
    int port = body.value("port", 80);
    if (port <= 0 || port > 65535) throw std::runtime_error("bad_port");
    return "http://" + host + ":" + std::to_string(port) + "/onvif/device_service";
}

} // namespace

void register_application_routes(
    crow::SimpleApp&                                                    app,
    store::ConfigStore&                                                 store,
    Auth&                                                               auth,
    CameraSupervisor*                                                   supervisor,
    const LicenseGate&                                                  license_gate,
    std::function<std::optional<Identity>(const crow::request&, Role)> require_auth) {

    CROW_ROUTE(app, "/api/v1/system/info")
    ([&store, &license_gate, &supervisor, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        json j;
        j["version"] = "0.3.0";
        j["time"]    = isoNow();
        std::string uptime;
        readFile("/proc/uptime", uptime);
        j["uptime"] = uptime.substr(0, uptime.find(' '));
        std::string hostname;
        readFile("/etc/hostname", hostname);
        while (!hostname.empty() && (hostname.back() == '\n' || hostname.back() == '\r'))
            hostname.pop_back();
        j["hostname"] = hostname;

        std::string cpu_model;
        std::string meminfo;
        readFile("/proc/meminfo", meminfo);
        std::ifstream cpuinfo("/proc/cpuinfo");
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.rfind("model name", 0) == 0) {
                auto pos = line.find(':');
                if (pos != std::string::npos) cpu_model = line.substr(pos + 1);
                break;
            }
        }
        j["cpu_model"]       = cpu_model;
        j["memory_kb_total"] = 0;
        size_t mp = meminfo.find("MemTotal:");
        if (mp != std::string::npos) {
            try {
                j["memory_kb_total"] = std::stoull(meminfo.substr(mp + 9));
            } catch (...) {}
        }

        json temps = json::array();
        for (int i = 0; i < 8; ++i) {
            std::string t;
            if (readFile("/sys/class/thermal/thermal_zone" + std::to_string(i) + "/temp", t)) {
                try {
                    temps.push_back(std::stod(t) / 1000.0);
                } catch (...) {}
            }
        }
        j["cpu_temps_c"]    = temps;
        j["cameras_active"] = supervisor ? supervisor->activeCameras().size() : 0;
        const size_t cam_cfg = store.listCameras().size();
        const std::string mqtt_delivery = store.db().getSetting("features.mqtt_delivery", "stub");
        j["features"] = json{{"mqtt", true},
                              {"mqtt_delivery", mqtt_delivery},
                              {"license", license_gate.statusJson(cam_cfg)}};
        return jsonResp(200, j);
    });

    CROW_ROUTE(app, "/api/v1/system/network").methods(crow::HTTPMethod::GET)
    ([&require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        auto out = runArgv({"/usr/sbin/ip", "-j", "addr"});
        crow::response r(out);
        r.set_header("Content-Type", "application/json");
        applySecurityHeaders(r);
        return r;
    });

    CROW_ROUTE(app, "/api/v1/system/network").methods(crow::HTTPMethod::PUT)
    ([&store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        try {
            auto b   = json::parse(req.body);
            auto yml = b.value("netplan_yaml", "");
            if (yml.empty()) return jsonResp(400, {{"error", "empty"}});
            if (yml.size() > 64 * 1024) return jsonResp(413, {{"error", "payload_too_large"}});
            const fs::path final_path("/etc/netplan/99-nvr.yaml");
            const fs::path tmp_path = final_path.string() + ".tmp";
            {
                std::ofstream f(tmp_path, std::ios::trunc | std::ios::binary);
                if (!f) return jsonResp(500, {{"error", "fs_open"}});
                f.write(yml.data(), static_cast<std::streamsize>(yml.size()));
                f.flush();
            }
            std::error_code ec;
            fs::rename(tmp_path, final_path, ec);
            if (ec) return jsonResp(500, {{"error", "fs_rename"}});
            runArgv({"/usr/bin/sudo", "-n", "/usr/sbin/netplan", "apply"});
            store.db().audit(who->login, "system.network.apply", "", "{}");
            return jsonResp(200, {{"ok", true}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/system/time").methods(crow::HTTPMethod::GET)
    ([&require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        auto out = runArgv({"/usr/bin/timedatectl", "show", "-p", "Timezone,NTPSynchronized,LocalRTC",
                            "--value"});
        return jsonResp(200, {{"raw", out}});
    });

    CROW_ROUTE(app, "/api/v1/system/time").methods(crow::HTTPMethod::PUT)
    ([&store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        try {
            auto b = json::parse(req.body);
            if (b.contains("timezone")) {
                auto tz = b["timezone"].get<std::string>();
                if (!isSafeTimezone(tz)) return jsonResp(400, {{"error", "bad_timezone"}});
                auto known = runArgv({"/usr/bin/timedatectl", "list-timezones"});
                if (known.find('\n' + tz + '\n') == std::string::npos && known.rfind(tz + '\n', 0) != 0)
                    return jsonResp(400, {{"error", "unknown_timezone"}});
                runArgv({"/usr/bin/sudo", "-n", "/usr/bin/timedatectl", "set-timezone", tz});
                store.db().audit(who->login, "system.time.timezone", tz, "{}");
            }
            return jsonResp(200, {{"ok", true}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/system/services/<string>/<string>").methods(crow::HTTPMethod::POST)
    ([&store, &require_auth](const crow::request& req, const std::string& name,
                             const std::string& action) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        if (kAllowedServices.count(name) == 0) return forbidden();
        if (kAllowedServiceActions.count(action) == 0)
            return jsonResp(400, {{"error", "bad_action"}});
        auto out = runArgv({"/usr/bin/sudo", "-n", "/usr/bin/systemctl", action, name});
        if (action != "status")
            store.db().audit(who->login, "system.service." + action, name, "{}");
        return jsonResp(200, {{"output", out}});
    });

    CROW_ROUTE(app, "/api/v1/system/logs")
    ([&require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        std::string unit = "nvr-prototype";
        int lines        = 200;
        if (auto v = req.url_params.get("unit")) unit = v;
        if (auto v = req.url_params.get("lines")) try { lines = std::stoi(v); } catch (...) {}
        if (kAllowedServices.count(unit) == 0) unit = "nvr-prototype";
        lines = clampLimit(lines, 1, kMaxLogLines);
        auto out = runArgv({"/usr/bin/journalctl", "-u", unit, "-n", std::to_string(lines),
                            "--no-pager", "-o", "short-iso"});
        return jsonResp(200, {{"log", out}});
    });

    CROW_ROUTE(app, "/api/v1/system/power/<string>").methods(crow::HTTPMethod::POST)
    ([&store, &require_auth](const crow::request& req, const std::string& act) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        if (kAllowedPower.count(act) == 0) return jsonResp(400, {{"error", "bad_action"}});
        const char* flag = (act == "reboot") ? "-r" : "-h";
        runArgvDetached({"/usr/bin/sudo", "-n", "/usr/sbin/shutdown", flag, "+1",
                         act == "reboot" ? "NVR reboot" : "NVR shutdown"});
        store.db().audit(who->login, "system.power", act, "{}");
        return jsonResp(200, {{"scheduled", true}});
    });

    CROW_ROUTE(app, "/api/v1/storage/disks")
    ([&require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        auto raw = runArgv({"/usr/bin/lsblk", "-J", "-O"});
        crow::response r(raw);
        r.set_header("Content-Type", "application/json");
        applySecurityHeaders(r);
        return r;
    });

    CROW_ROUTE(app, "/api/v1/storage/disks/<string>/smart")
    ([&require_auth](const crow::request& req, const std::string& dev) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        if (!isSafeBlockDevice(dev)) return jsonResp(400, {{"error", "bad_dev"}});
        fs::path devp = fs::path("/dev") / dev;
        std::error_code ec;
        auto st = fs::symlink_status(devp, ec);
        if (ec || fs::is_symlink(st)) return jsonResp(400, {{"error", "bad_dev"}});
        auto raw = runArgv({"/usr/bin/sudo", "-n", "/usr/sbin/smartctl", "-aj", devp.string()});
        crow::response r(raw);
        r.set_header("Content-Type", "application/json");
        applySecurityHeaders(r);
        return r;
    });

    CROW_ROUTE(app, "/api/v1/storage/usage")
    ([&require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        auto raw = runArgv(
            {"/usr/bin/df", "-h", "--output=source,size,used,avail,pcent,target"});
        return jsonResp(200, {{"df", raw},
                            {"archive_used_ratio",
                             obs::Registry::instance().global().archive_used_ratio.value()}});
    });

    CROW_ROUTE(app, "/api/v1/events/search")
    ([&store, &license_gate, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        std::string obj, cam, from, to;
        if (auto v = req.url_params.get("object")) obj = v;
        if (auto v = req.url_params.get("camera_id")) cam = v;
        if (auto v = req.url_params.get("from")) from = v;
        if (auto v = req.url_params.get("to")) to = v;
        if (obj.empty()) return jsonResp(400, {{"error", "object_required"}});

        const bool motion_query = (obj == "motion");
        if (!motion_query) {
            const unsigned mod_bit = (obj == "face") ? 2u : 0u;
            if (!license_gate.allowAiModule(mod_bit)) {
                return jsonResp(403,
                                 {{"error", "license_mod_required"},
                                  {"bit", mod_bit},
                                  {"message", "Licensed AI module bit required for this object type "
                                              "(see docs/LICENSE_TICKET.md)"}});
            }
        }

        std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
        std::string sql = "SELECT id, camera_id, ts, object_type, confidence, track_id, bbox_json "
                          "FROM ai_detections WHERE ";
        std::string bind_type = obj;
        if (obj == "onvif") {
            sql += "object_type LIKE ?";
            bind_type = "onvif%";
        } else if (obj.size() > 6 && obj.rfind("onvif.", 0) == 0) {
            sql += "object_type LIKE ?";
            bind_type = obj + "%";
        } else {
            sql += "object_type=?";
        }
        if (!cam.empty()) sql += " AND camera_id=?";
        if (!from.empty()) sql += " AND ts >= ?";
        if (!to.empty()) sql += " AND ts <= ?";
        sql += " ORDER BY id DESC LIMIT 500";
        SQLite::Statement q(store.db().raw(), sql);
        int idx = 1;
        q.bind(idx++, bind_type);
        if (!cam.empty()) q.bind(idx++, cam);
        if (!from.empty()) q.bind(idx++, from);
        if (!to.empty()) q.bind(idx++, to);
        json out = json::array();
        while (q.executeStep()) {
            out.push_back({{"id", q.getColumn(0).getInt64()},
                           {"camera_id", q.getColumn(1).getString()},
                           {"ts", q.getColumn(2).getString()},
                           {"type", q.getColumn(3).getString()},
                           {"confidence", q.getColumn(4).getDouble()},
                           {"track_id", q.getColumn(5).getInt64()},
                           {"bbox", json::parse(q.getColumn(6).getString(), nullptr, false)}});
        }
        return jsonResp(200, out);
    });

    CROW_ROUTE(app, "/api/v1/ai/models")
    ([&store, &license_gate, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        if (!license_gate.allowAiModule(0)) {
            return jsonResp(403,
                             {{"error", "license_mod_required"},
                              {"bit", 0},
                              {"message", "Licensed AI module bit 0 required (see docs/LICENSE_TICKET.md)"}});
        }
        std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
        SQLite::Statement q(store.db().raw(),
                              "SELECT id, kind, path, precision, device, enabled FROM ai_models ORDER BY id");
        json out = json::array();
        while (q.executeStep()) {
            out.push_back({{"id", q.getColumn(0).getInt64()},
                           {"kind", q.getColumn(1).getString()},
                           {"path", q.getColumn(2).getString()},
                           {"precision", q.getColumn(3).getString()},
                           {"device", q.getColumn(4).getString()},
                           {"enabled", q.getColumn(5).getInt() != 0}});
        }
        return jsonResp(200, out);
    });

    CROW_ROUTE(app, "/api/v1/ai/models").methods(crow::HTTPMethod::POST)
    ([&store, &license_gate, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        if (!license_gate.allowAiModule(0)) {
            return jsonResp(403,
                             {{"error", "license_mod_required"},
                              {"bit", 0},
                              {"message", "Licensed AI module bit 0 required (see docs/LICENSE_TICKET.md)"}});
        }
        try {
            auto b = json::parse(req.body);
            std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
            SQLite::Statement q(store.db().raw(),
                                  "INSERT INTO ai_models(kind, path, precision, device, enabled) "
                                  "VALUES(?,?,?,?,1)");
            q.bind(1, b.value("kind", ""));
            q.bind(2, b.value("path", ""));
            q.bind(3, b.value("precision", "FP16"));
            q.bind(4, b.value("device", "CPU"));
            q.exec();
            int64_t id = store.db().raw().getLastInsertRowid();
            store.db().audit(who->login, "ai.model.create", std::to_string(id), req.body);
            return jsonResp(201, {{"id", id}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/ai/models/<int>").methods(crow::HTTPMethod::PATCH)
    ([&store, &license_gate, &require_auth](const crow::request& req, int id) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        if (!license_gate.allowAiModule(0)) {
            return jsonResp(403,
                             {{"error", "license_mod_required"},
                              {"bit", 0},
                              {"message", "Licensed AI module bit 0 required (see docs/LICENSE_TICKET.md)"}});
        }
        try {
            auto b = json::parse(req.body);
            std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
            if (b.contains("enabled")) {
                SQLite::Statement q(store.db().raw(), "UPDATE ai_models SET enabled=? WHERE id=?");
                q.bind(1, b["enabled"].get<bool>() ? 1 : 0);
                q.bind(2, id);
                q.exec();
                store.db().audit(who->login, "ai.model.set_enabled", std::to_string(id),
                                 json{{"enabled", b["enabled"].get<bool>()}}.dump());
            }
            return jsonResp(200, {{"ok", true}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/ai/faces")
    ([&store, &license_gate, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Operator);
        if (!who) return forbidden();
        if (!license_gate.allowAiModule(2)) {
            return jsonResp(403,
                             {{"error", "license_mod_required"},
                              {"bit", 2},
                              {"message", "Licensed AI module bit 2 (faces) required (see docs/LICENSE_TICKET.md)"}});
        }
        std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
        SQLite::Statement q(store.db().raw(), "SELECT id, name FROM face_persons ORDER BY id");
        json out = json::array();
        while (q.executeStep()) {
            out.push_back(
                {{"id", q.getColumn(0).getInt64()}, {"name", q.getColumn(1).getString()}});
        }
        return jsonResp(200, out);
    });

    CROW_ROUTE(app, "/api/v1/kiosk/token").methods(crow::HTTPMethod::POST)
    ([&auth, &store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        Identity kid;
        kid.login = "kiosk";
        kid.role  = Role::Viewer;
        auto tok  = auth.issueToken(kid, std::chrono::hours{24 * 180}, "viewer:local");
        store.db().audit(who->login, "kiosk.token_issue", "kiosk", "{}");
        return jsonResp(200, {{"token", tok}});
    });

    CROW_ROUTE(app, "/api/v1/kiosk/exchange").methods(crow::HTTPMethod::POST)
    ([&auth](const crow::request& req) {
        std::string tok;
        try {
            auto body = json::parse(req.body, nullptr, false);
            if (!body.is_discarded()) tok = body.value("token", "");
        } catch (...) {}
        if (tok.empty()) return unauthorized();
        auto id = auth.verify(tok);
        if (!id) return unauthorized();
        if (id->scope != "viewer:local") return forbidden();
        auto access = auth.issueToken(*id, std::chrono::hours{12}, "viewer:local");
        crow::response r(200, json{{"token", access}, {"expires_in", 12 * 3600}}.dump());
        r.set_header("Content-Type", "application/json");
        std::ostringstream c;
        c << "nvr_token=" << access << "; Path=/"
          << "; Max-Age=" << 12 * 3600 << "; HttpOnly; SameSite=Strict; Secure";
        r.add_header("Set-Cookie", c.str());
        applySecurityHeaders(r);
        return r;
    });

    CROW_ROUTE(app, "/api/v1/setup/status")([] {
        bool first_run = !fs::exists(kSetupMarker);
        return jsonResp(200, {{"first_run", first_run}});
    });

    CROW_ROUTE(app, "/api/v1/setup/finalize").methods(crow::HTTPMethod::POST)
    ([&store, &auth](const crow::request& req) {
        if (fs::exists(kSetupMarker)) return forbidden();

        auto header_tok = req.get_header_value("X-Setup-Token");
        std::string stored_tok;
        if (!readFile(kSetupToken, stored_tok)) return forbidden();
        while (!stored_tok.empty() && (stored_tok.back() == '\n' || stored_tok.back() == '\r'))
            stored_tok.pop_back();
        if (stored_tok.empty() || header_tok != stored_tok) return forbidden();

        try {
            auto body         = json::parse(req.body);
            auto hostname     = body.value("hostname", "");
            auto pass         = body.value("admin_password", "");
            auto tz           = body.value("tz", "");
            auto archive_root = body.value("archive_root", "");

            if (pass.size() < 12) return jsonResp(400, {{"error", "password_too_short"}});
            for (char c : pass)
                if (static_cast<unsigned char>(c) < 0x20)
                    return jsonResp(400, {{"error", "password_invalid_chars"}});
            if (!hostname.empty() && !isSafeHostname(hostname))
                return jsonResp(400, {{"error", "bad_hostname"}});
            if (!tz.empty() && !isSafeTimezone(tz))
                return jsonResp(400, {{"error", "bad_timezone"}});
            if (!archive_root.empty()) {
                fs::path ap(archive_root);
                if (!ap.is_absolute() || ap.string().find("..") != std::string::npos)
                    return jsonResp(400, {{"error", "bad_archive_root"}});
            }

            if (!auth.createUser("admin", pass, Role::Admin)) auth.changePassword("admin", pass);
            auth.setUserEnabled("admin", true);

            if (!hostname.empty())
                runArgv({"/usr/bin/sudo", "-n", "/usr/bin/hostnamectl", "set-hostname", hostname});
            if (!tz.empty()) runArgv({"/usr/bin/sudo", "-n", "/usr/bin/timedatectl", "set-timezone", tz});
            if (!archive_root.empty()) {
                auto a = store.archiveConfig();
                a.root_path = archive_root;
                store.setArchiveConfig(a);
            }
            std::string tmp = std::string(kSetupMarker) + ".tmp";
            {
                std::ofstream m(tmp, std::ios::trunc);
                m << isoNow();
                if (!m.flush() || !m.good()) {
                    std::error_code ec_w;
                    fs::remove(tmp, ec_w);
                    return jsonResp(500, {{"error", "setup_marker_write_failed"}});
                }
            }
#ifndef _WIN32
            {
                int fd = ::open(tmp.c_str(), O_RDWR);
                if (fd >= 0) {
                    ::fsync(fd);
                    ::close(fd);
                }
                const std::string parent = fs::path(kSetupMarker).parent_path().string();
                if (!parent.empty()) {
                    int dfd = ::open(parent.c_str(), O_RDONLY);
                    if (dfd >= 0) {
                        ::fsync(dfd);
                        ::close(dfd);
                    }
                }
            }
#endif
            std::error_code ec_m;
            fs::rename(tmp, kSetupMarker, ec_m);
            if (ec_m) {
                std::error_code ec_r;
                fs::remove(tmp, ec_r);
                return jsonResp(500, {{"error", "setup_marker_rename_failed"}});
            }
            std::error_code ec;
            fs::remove(kSetupToken, ec);
            store.db().audit("admin", "setup.finalize", hostname,
                             json{{"tz", tz}, {"archive_root", archive_root}}.dump());
            return jsonResp(200, {{"ok", true}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/onvif/discover")
    ([&require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        auto devs = onvif::probe();
        json arr  = json::array();
        for (auto& d : devs)
            arr.push_back({{"endpoint", d.endpoint},
                           {"xaddrs", d.xaddrs},
                           {"types", d.types},
                           {"scopes", d.scopes}});
        return jsonResp(200, arr);
    });

    CROW_ROUTE(app, "/api/v1/onvif/enumerate-streams").methods(crow::HTTPMethod::POST)
    ([&require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        try {
            auto body = json::parse(req.body);
            const std::string devUrl = resolveOnvifDeviceServiceUrl(body);
            const std::string user   = body.value("user", "");
            const std::string pass   = body.value("pass", "");
            auto              r      = onvif::enumerateStreams(devUrl, user, pass);
            if (!r.ok) {
                return jsonResp(502, {{"error", "onvif_enumerate_failed"},
                                    {"detail", r.error},
                                    {"device_service_url", r.device_service_url},
                                    {"media_service_url", r.media_service_url},
                                    {"onvif_host", r.onvif_host},
                                    {"onvif_port", r.onvif_port}});
            }
            json streams = json::array();
            for (const auto& s : r.streams) {
                streams.push_back({{"profile_token", s.profile_token},
                                   {"profile_name", s.profile_name},
                                   {"width", s.width},
                                   {"height", s.height},
                                   {"codec", s.codec},
                                   {"uri", s.uri}});
            }
            json jdev = nullptr;
            if (r.device) {
                jdev = json{{"manufacturer", r.device->manufacturer},
                             {"model", r.device->model},
                             {"firmware", r.device->firmware},
                             {"serial", r.device->serial}};
            }
            return jsonResp(200,
                            {{"device_service_url", r.device_service_url},
                             {"media_service_url", r.media_service_url},
                             {"onvif_host", r.onvif_host},
                             {"onvif_port", r.onvif_port},
                             {"device", jdev},
                             {"streams", streams}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/onvif/device-time-sync").methods(crow::HTTPMethod::POST)
    ([&require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        try {
            auto              body   = json::parse(req.body);
            const std::string devUrl = resolveOnvifDeviceServiceUrl(body);
            const std::string user   = body.value("user", "");
            const std::string pass   = body.value("pass", "");
            onvif::SoapClient cli(devUrl, user, pass);
            const bool ok = cli.setSystemDateAndTimeUtc(std::chrono::system_clock::now());
            return jsonResp(ok ? 200 : 502, {{"ok", ok}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/onvif/<string>/ptz").methods(crow::HTTPMethod::POST)
    ([&store, &require_auth](const crow::request& req, const std::string& cam_id) {
        auto who = require_auth(req, Role::Operator);
        if (!who) return forbidden();
        auto cam = store.getCamera(cam_id);
        if (!cam || cam->onvif_host.empty()) return notFound();
        if (!cameraVisibleToUser(store, *who, cam_id)) return forbidden();
        try {
            auto body = json::parse(req.body);
            onvif::PtzVector v;
            v.pan  = body.value("pan", 0.0);
            v.tilt = body.value("tilt", 0.0);
            v.zoom = body.value("zoom", 0.0);
            std::string endpoint = "http://" + cam->onvif_host + ":" +
                                   std::to_string(cam->onvif_port) + "/onvif/ptz_service";
            onvif::SoapClient cli(endpoint, cam->onvif_user, cam->onvif_pass);
            const std::string profile_token = body.value("profile", "Profile_1");
            const bool        stop          = body.value("stop", false);
            bool ok = stop ? cli.ptzStop(endpoint, profile_token)
                           : cli.ptzMove(endpoint, profile_token, v);
            if (ok && stop) store.db().audit(who->login, "onvif.ptz.stop", cam_id, "{}");
            return jsonResp(ok ? 200 : 502, {{"ok", ok}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/system")
    ([&store, &supervisor, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        const std::string mqtt_delivery = store.db().getSetting("features.mqtt_delivery", "stub");
        return jsonResp(200,
                         {{"version", "0.3.0-prototype"},
                          {"time", isoNow()},
                          {"cameras_active", supervisor ? supervisor->activeCameras().size() : 0},
                          {"features", {{"mqtt", true}, {"mqtt_delivery", mqtt_delivery}}}});
    });
}

} // namespace nvr::api
