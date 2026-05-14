#include "nvr/api/RouteCamerasLicense.hpp"

#include "nvr/api/HttpCommon.hpp"
#include "nvr/api/HttpRouteHelpers.hpp"

#include "nvr/EventBus.hpp"
#include "nvr/onvif/EventsPull.hpp"
#include "nvr/obs/Metrics.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

#include <crow/app.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <regex>
#include <sys/socket.h>

#include <cctype>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace nvr::api {

namespace {

std::string onvifTypeFromTopic(const std::string& topic) {
    std::string t = "onvif";
    for (char c : topic) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
            t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (c == '/' || c == ':')
            t += '.';
        if (t.size() >= 120) break;
    }
    return t;
}

} // namespace

void register_cameras_license_routes(
    crow::SimpleApp&                                                    app,
    store::ConfigStore&                                                 store,
    CameraSupervisor*                                                   supervisor,
    const LicenseGate&                                                  license_gate,
    EventBus*                                                           event_bus,
    std::function<std::optional<Identity>(const crow::request&, Role)> require_auth) {

    (void)supervisor;

    CROW_ROUTE(app, "/api/v1/cameras").methods(crow::HTTPMethod::GET)
    ([&store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        auto list = store.listCameras();
        json arr  = json::array();
        for (auto& c : list) {
            if (!cameraVisibleToUser(store, *who, c.id)) continue;
            arr.push_back(json::parse(cameraToJson(c)));
        }
        return jsonResp(200, arr);
    });

    CROW_ROUTE(app, "/api/v1/cameras").methods(crow::HTTPMethod::POST)
    ([&store, &license_gate, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        const auto existing = store.listCameras();
        if (existing.size() >= static_cast<size_t>(license_gate.effectiveMaxChannels())) {
            return jsonResp(403, {{"error", "license_channel_limit"},
                                  {"max_channels", license_gate.effectiveMaxChannels()},
                                  {"configured", existing.size()}});
        }
        try {
            auto body = json::parse(req.body);
            auto c    = jsonToCamera(body);
            if (c.id.empty()) return jsonResp(400, {{"error", "id_required"}});
            if (c.recording_mode == RecordingMode::Motion && !c.enable_motion) {
                return jsonResp(400, {{"error", "motion_recording_requires_enable_motion"}});
            }
            if (c.recording_mode == RecordingMode::Hybrid && !c.enable_motion) {
                return jsonResp(400, {{"error", "hybrid_requires_enable_motion"}});
            }
            store.upsertCamera(c, who->login);
            return crow::response(201, cameraToJson(c));
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/license")
    ([&store, &license_gate, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        const auto cams = store.listCameras();
        return jsonResp(200, license_gate.statusJson(cams.size()));
    });

    CROW_ROUTE(app, "/api/v1/cameras/<string>").methods(crow::HTTPMethod::PATCH)
    ([&store, &require_auth](const crow::request& req, const std::string& id) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        auto existing = store.getCamera(id);
        if (!existing) return notFound();
        try {
            auto body   = json::parse(req.body);
            auto merged = json::parse(cameraToJson(*existing));
            for (auto it = body.begin(); it != body.end(); ++it) merged[it.key()] = it.value();
            if (body.contains("onvif_pass")) merged["onvif_pass"] = body["onvif_pass"];
            auto c = jsonToCamera(merged, id);
            if (c.recording_mode == RecordingMode::Motion && !c.enable_motion) {
                return jsonResp(400, {{"error", "motion_recording_requires_enable_motion"}});
            }
            if (c.recording_mode == RecordingMode::Hybrid && !c.enable_motion) {
                return jsonResp(400, {{"error", "hybrid_requires_enable_motion"}});
            }
            store.upsertCamera(c, who->login);
            return crow::response(200, cameraToJson(c));
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/cameras/<string>").methods(crow::HTTPMethod::DELETE)
    ([&store, &require_auth](const crow::request& req, const std::string& id) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        store.deleteCamera(id, who->login);
        return crow::response(204);
    });

    CROW_ROUTE(app, "/api/v1/cameras/<string>/snapshot.jpg")
    ([&store, &require_auth](const crow::request& req, const std::string& id) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        auto motion_cfg = store.motionConfig();
        auto cam        = store.getCamera(id);
        if (!cam) return notFound();
        if (!cameraVisibleToUser(store, *who, id)) return forbidden();
        fs::path dir(motion_cfg.snapshot_dir);
        fs::path latest;
        std::filesystem::file_time_type best_time{};
        std::error_code ec;
        for (auto& e : fs::directory_iterator(dir, ec)) {
            if (!e.is_regular_file(ec)) continue;
            auto name = e.path().filename().string();
            if (name.rfind(id + "_", 0) != 0) continue;
            auto t = fs::last_write_time(e.path(), ec);
            if (latest.empty() || t > best_time) {
                latest    = e.path();
                best_time = t;
            }
        }
        if (latest.empty()) return notFound();
        std::string buf;
        if (!readFile(latest, buf)) return notFound();
        crow::response r(buf);
        r.set_header("Content-Type", "image/jpeg");
        return r;
    });

    CROW_ROUTE(app, "/api/v1/cameras/<string>/stream-stats")
    ([&store, &require_auth](const crow::request& req, const std::string& id) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        if (!store.getCamera(id)) return notFound();
        if (!cameraVisibleToUser(store, *who, id)) return forbidden();
        auto& m = nvr::obs::Registry::instance().camera(id);
        return jsonResp(200,
                         {{"camera_id", id},
                          {"fps", m.fps.value()},
                          {"bitrate_kbps", m.bitrate_kbps.value()},
                          {"pipeline_lag_ms", m.pipeline_lag_ms.value()},
                          {"state", m.state.value()},
                          {"frames_received_total", m.frames_received_total.value()},
                          {"frames_dropped_total", m.frames_dropped_total.value()},
                          {"decoder_errors_total", m.decoder_errors_total.value()},
                          {"rtsp_reconnects_total", m.rtsp_reconnects_total.value()},
                          {"bytes_in_total", m.bytes_in_total.value()},
                          {"bytes_recorded_total", m.bytes_recorded_total.value()},
                          {"inference_ms", m.inference_ms.value()},
                          {"inference_fps", m.inference_fps.value()}});
    });

    CROW_ROUTE(app, "/api/v1/cameras/<string>/onvif-events")
    ([&store, &require_auth](const crow::request& req, const std::string& id) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        if (!store.getCamera(id)) return notFound();
        if (!cameraVisibleToUser(store, *who, id)) return forbidden();
        int limit = 100;
        if (auto l = req.url_params.get("limit")) try { limit = std::stoi(l); } catch (...) {}
        limit = clampLimit(limit, 1, kMaxEventsLimitArchive);
        std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
        SQLite::Statement q(
            store.db().raw(),
            "SELECT id, camera_id, ts, type, severity, payload_json, snapshot_path, "
            "clip_path, acknowledged FROM events WHERE camera_id=? AND type LIKE 'onvif.%' "
            "ORDER BY id DESC LIMIT ?");
        q.bind(1, id);
        q.bind(2, limit);
        json out = json::array();
        while (q.executeStep()) {
            out.push_back({
                {"id",            q.getColumn(0).getInt64()},
                {"camera_id",     q.getColumn(1).isNull() ? "" : q.getColumn(1).getString()},
                {"ts",            q.getColumn(2).getString()},
                {"type",          q.getColumn(3).getString()},
                {"severity",      q.getColumn(4).getString()},
                {"payload",       json::parse(q.getColumn(5).getString(), nullptr, false)},
                {"snapshot_path", q.getColumn(6).isNull() ? "" : q.getColumn(6).getString()},
                {"clip_path",     q.getColumn(7).isNull() ? "" : q.getColumn(7).getString()},
                {"acknowledged",  q.getColumn(8).getInt() != 0},
            });
        }
        return jsonResp(200, out);
    });

    CROW_ROUTE(app, "/api/v1/cameras/<string>/onvif-events/ingest").methods(crow::HTTPMethod::POST)
    ([&store, event_bus, &require_auth](const crow::request& req, const std::string& id) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        if (!store.getCamera(id)) return notFound();
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return jsonResp(400, {{"error", "bad_json"}});
        }
        if (!body.is_object()) return jsonResp(400, {{"error", "bad_json"}});
        std::string type = body.value("type", std::string("onvif.mvp.signal"));
        if (!type.starts_with("onvif.")) {
            return jsonResp(400, {{"error", "onvif_type_prefix_required"},
                                {"message", "type must start with onvif."}});
        }
        std::string severity = body.value("severity", std::string("info"));
        json        payload  = (body.contains("payload") && body["payload"].is_object()) ? body["payload"]
                                                                                         : json::object();
        int64_t eid = 0;
        {
            std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
            eid = store.db().insertEvent(id, type, severity, payload.dump(), "", "");
            store.db().audit(who->login, "onvif.event.ingest", id, req.body);
        }
        if (event_bus) {
            SystemEvent ev;
            ev.camera_id    = id;
            ev.type         = type;
            ev.severity     = severity;
            ev.payload_json = payload.dump();
            event_bus->publish(std::move(ev));
        }
        return jsonResp(201, {{"id", eid}, {"camera_id", id}, {"type", type}});
    });

    CROW_ROUTE(app, "/api/v1/cameras/<string>/onvif-events/pull-once").methods(crow::HTTPMethod::POST)
    ([&store, event_bus, &require_auth](const crow::request& req, const std::string& id) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        auto cam = store.getCamera(id);
        if (!cam) return notFound();
        if (cam->onvif_host.empty())
            return jsonResp(400, {{"error", "onvif_host_required"}});

        std::optional<std::string> events_override;
        if (!req.body.empty()) {
            try {
                auto body = json::parse(req.body, nullptr, false);
                if (body.is_object() && body.contains("events_service_url") &&
                    body["events_service_url"].is_string()) {
                    events_override = body["events_service_url"].get<std::string>();
                }
            } catch (...) {}
        }

        const std::string events_url = nvr::onvif::resolveEventsServiceUrl(
            cam->onvif_host, cam->onvif_port, cam->onvif_user, cam->onvif_pass, events_override);
        const auto pulled = nvr::onvif::pullPointOnce(events_url, cam->onvif_user, cam->onvif_pass);

        json items = json::array();
        for (const auto& pe : pulled) {
            const std::string type = onvifTypeFromTopic(pe.topic);
            json              payload{{"topic", pe.topic}, {"raw_snippet", pe.raw_snippet}};
            int64_t           eid = 0;
            {
                std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
                eid = store.db().insertEvent(id, type, "info", payload.dump(), "", "");
                store.db().audit(who->login, "onvif.pullpoint", id, type);
            }
            if (event_bus) {
                SystemEvent ev;
                ev.camera_id    = id;
                ev.type         = type;
                ev.severity     = "info";
                ev.payload_json = payload.dump();
                event_bus->publish(std::move(ev));
            }
            items.push_back({{"type", type}, {"id", eid}});
        }
        return jsonResp(200, {{"camera_id", id}, {"count", pulled.size()}, {"events", items}});
    });

    CROW_ROUTE(app, "/api/v1/cameras/test-rtsp").methods(crow::HTTPMethod::POST)
    ([&require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        try {
            auto body = json::parse(req.body);
            auto url  = body.value("rtsp_url", "");
            if (url.empty()) return jsonResp(400, {{"error", "url_required"}});
            std::regex rx(R"(rtsp://(?:[^@/]+@)?([A-Za-z0-9._-]+)(?::(\d+))?(?:/.*)?$)");
            std::smatch m;
            if (!std::regex_match(url, m, rx)) return jsonResp(400, {{"error", "bad_url"}});
            auto host = m[1].str();
            int  port = m[2].matched ? std::stoi(m[2].str()) : 554;
            if (port <= 0 || port > 65535) return jsonResp(400, {{"error", "bad_port"}});
            if (!isSafeHostname(host)) return jsonResp(400, {{"error", "bad_host"}});

            int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
            bool reachable = false;
            if (fd >= 0) {
                sockaddr_in sa{};
                sa.sin_family = AF_INET;
                sa.sin_port   = htons(static_cast<uint16_t>(port));
                addrinfo hints{};
                hints.ai_family   = AF_INET;
                hints.ai_socktype = SOCK_STREAM;
                addrinfo* res = nullptr;
                if (::getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 && res) {
                    sa.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
                    ::freeaddrinfo(res);
                    ::connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
                    pollfd pf{fd, POLLOUT, 0};
                    if (::poll(&pf, 1, 3000) > 0 && (pf.revents & POLLOUT)) {
                        int err      = 0;
                        socklen_t sl = sizeof(err);
                        ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &sl);
                        reachable = (err == 0);
                    }
                }
                ::close(fd);
            }
            return jsonResp(200, {{"reachable", reachable}, {"host", host}, {"port", port}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });
}

} // namespace nvr::api
