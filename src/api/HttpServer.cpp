#include "nvr/api/HttpServer.hpp"

#include "nvr/Logger.hpp"
#include "nvr/api/HttpCommon.hpp"
#include "nvr/api/HttpRouteHelpers.hpp"
#include "nvr/api/RouteApplication.hpp"
#include "nvr/api/RouteArchiveEvents.hpp"
#include "nvr/api/RouteAuth.hpp"
#include "nvr/api/RouteCamerasLicense.hpp"
#include "nvr/api/RouteConfig.hpp"
#include "nvr/api/RouteInfra.hpp"
#include "nvr/api/RouteLiveLayouts.hpp"
#include "nvr/api/RouteNotifications.hpp"
#include "nvr/api/RouteStream.hpp"
#include "nvr/api/RouteUsersAudit.hpp"

#define CROW_MAIN
#include <crow.h>
#include <crow/app.h>
#include <crow/websocket.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace nvr::api {

struct HttpServer::Impl {
    HttpConfig                    cfg;
    store::ConfigStore&           store;
    Auth&                         auth;
    CameraSupervisor*             supervisor;
    EventBus*                     event_bus;
    notify::NotificationManager*  notify_mgr;
    const LicenseGate&            license_gate;
    uint64_t                      ev_sub_id{0};

    crow::SimpleApp               app;
    std::thread                   serve_thread;

    std::mutex                                wsmu;
    std::set<crow::websocket::connection*>    ws_conns;

    // rate-limit (Phase E3 prep)
    std::mutex                                                       rl_mu;
    std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> rl_log;

    Impl(HttpConfig c, store::ConfigStore& s, Auth& a,
         CameraSupervisor* sv, EventBus* eb, notify::NotificationManager* nm,
         const LicenseGate& lg)
        : cfg(std::move(c)), store(s), auth(a),
          supervisor(sv), event_bus(eb), notify_mgr(nm), license_gate(lg) {}
};

HttpServer::HttpServer(HttpConfig                  cfg,
                       store::ConfigStore&         store,
                       Auth&                       auth,
                       CameraSupervisor*           sv,
                       EventBus*                   eb,
                       notify::NotificationManager* nm,
                       const LicenseGate&          license_gate)
    : impl_(std::make_unique<Impl>(std::move(cfg), store, auth, sv, eb, nm, license_gate)) {}

HttpServer::~HttpServer() { stop(); }

void HttpServer::stop() {
    if (!impl_) return;
    if (impl_->event_bus && impl_->ev_sub_id) {
        impl_->event_bus->unsubscribe(impl_->ev_sub_id);
        impl_->ev_sub_id = 0;
    }
    impl_->app.stop();
    if (impl_->serve_thread.joinable()) impl_->serve_thread.join();
}

void HttpServer::start() {
    auto& impl = *impl_;

    auto require_auth = [&impl](const crow::request& req, Role required) -> std::optional<Identity> {
        auto tok = extractAuthToken(req);
        if (!tok) return std::nullopt;
        auto id = impl.auth.verify(*tok);
        if (!id) return std::nullopt;
        if (!roleAtLeast(id->role, required)) return std::nullopt;
        return id;
    };

    auto require_auth_stream = [&impl](const crow::request& req, Role required) -> std::optional<Identity> {
        auto tok = extractStreamToken(req);
        if (!tok) return std::nullopt;
        auto id = impl.auth.verify(*tok);
        if (!id) return std::nullopt;
        if (!roleAtLeast(id->role, required)) return std::nullopt;
        return id;
    };

    auto rate_limit_login = [&impl](const std::string& ip) -> bool {
        std::lock_guard<std::mutex> lk(impl.rl_mu);
        auto now = std::chrono::steady_clock::now();
        auto& q  = impl.rl_log[ip];
        while (!q.empty() && now - q.front() > std::chrono::seconds(60)) q.pop_front();
        if (q.size() >= 5) return false;
        q.push_back(now);
        return true;
    };

    register_infra_health_metrics_branding_routes(
        impl.app, impl.store, impl.auth, impl.supervisor);
    register_auth_routes(impl.app, impl.auth, impl.store, require_auth, rate_limit_login);
    register_cameras_license_routes(
        impl.app, impl.store, impl.supervisor, impl.license_gate, impl.event_bus, require_auth);
    register_archive_event_routes(impl.app, impl.store, require_auth, require_auth_stream);
    register_users_audit_routes(impl.app, impl.auth, impl.store, require_auth);
    register_config_motion_archive_routes(impl.app, impl.store, require_auth);
    register_live_layout_and_field_routes(impl.app, impl.store, impl.supervisor, require_auth);
    register_notification_routes(
        impl.app, impl.store, impl.notify_mgr, impl.event_bus, require_auth);
    register_application_routes(
        impl.app, impl.store, impl.auth, impl.supervisor, impl.license_gate, require_auth);
    register_stream_routes(
        impl.app, impl.cfg, impl.store, impl.auth, impl.wsmu, impl.ws_conns, require_auth_stream);

    if (impl.event_bus) {
        impl.ev_sub_id = impl.event_bus->subscribe([&impl](const SystemEvent& ev) {
            std::string payload = json{
                {"channel",       "events"},
                {"camera_id",     ev.camera_id},
                {"type",          ev.type},
                {"severity",      ev.severity},
                {"snapshot_path", ev.snapshot_path},
                {"clip_path",     ev.clip_path},
                {"payload",       json::parse(ev.payload_json, nullptr, false)},
            }.dump();
            std::lock_guard<std::mutex> lk(impl.wsmu);
            for (auto* c : impl.ws_conns) {
                try { c->send_text(payload); } catch (...) {}
            }
        });
    }

    CROW_ROUTE(impl.app, "/docs")([] {
        crow::response r(302, ""); r.set_header("Location", "/web/docs/"); return r;
    });

    CROW_ROUTE(impl.app, "/sw.js")
    ([&impl](const crow::request&) {
        fs::path p = fs::path(impl.cfg.web_root) / "sw.js";
        if (!pathInsideRoot(p, fs::path(impl.cfg.web_root)) || !isRegularFileNoSymlink(p))
            return notFound();
        std::string body;
        if (!readFile(p, body)) return notFound();
        crow::response r(body);
        r.set_header("Content-Type", "application/javascript; charset=utf-8");
        r.set_header("Cache-Control", "no-cache");
        r.set_header("Service-Worker-Allowed", "/");
        applySecurityHeaders(r);
        return r;
    });

    // ------------------------------------------------------------------ SPA web_root fallback
    CROW_ROUTE(impl.app, "/")
    ([&impl] {
        std::string body;
        readFile(fs::path(impl.cfg.web_root) / "index.html", body);
        crow::response r(body);
        r.set_header("Content-Type", "text/html; charset=utf-8");
        applySecurityHeaders(r, /*html=*/true);
        return r;
    });

    CROW_ROUTE(impl.app, "/web/<path>")
    ([&impl](const crow::request&, const std::string& subpath) {
        // Reject obvious traversal payloads early.
        if (subpath.find("..") != std::string::npos ||
            subpath.find('\\') != std::string::npos ||
            subpath.find('\0') != std::string::npos) return notFound();
        fs::path p = fs::path(impl.cfg.web_root) / subpath;
        if (!pathInsideRoot(p, fs::path(impl.cfg.web_root)) || !isRegularFileNoSymlink(p))
            return notFound();
        std::string body;
        if (!readFile(p, body)) return notFound();
        crow::response r(body);
        r.set_header("Content-Type", mimeFor(p));
        if (p.extension() == ".html") r.set_header("Cache-Control", "no-cache");
        else                          r.set_header("Cache-Control", "public, max-age=86400");
        applySecurityHeaders(r, /*html=*/p.extension() == ".html");
        return r;
    });

    impl.app.loglevel(crow::LogLevel::Warning);

    // TLS termination is owned by nginx in front of the daemon (see
    // deploy/nginx/nvr.conf). The application listens HTTP on a loopback
    // address (default 127.0.0.1:8080) so the link to nginx never leaves
    // the host. If you really want to expose this socket directly, change
    // `bind_address` in /etc/nvr-prototype/config.yaml — but Set-Cookie
    // `Secure` and HSTS require TLS in front of it.
    impl.serve_thread = std::thread([&impl] {
        try {
            unsigned workers = impl.cfg.http_worker_threads > 0
                ? static_cast<unsigned>(impl.cfg.http_worker_threads)
                : std::thread::hardware_concurrency();
            if (workers == 0) workers = 2;
            if (workers < 2) workers = 2;
            impl.app
                .concurrency(workers)
                .bindaddr(impl.cfg.bind_address)
                .port(static_cast<uint16_t>(impl.cfg.port_http))
                .multithreaded()
                .run();
        } catch (const std::exception& e) {
            NVR_ERROR("http", "server exception: %s", e.what());
        }
    });

    NVR_INFO("http", "HTTP server listening on %s:%d (TLS handled upstream by nginx)",
             impl.cfg.bind_address.c_str(), impl.cfg.port_http);
}

}
