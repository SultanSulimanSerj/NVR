#include "nvr/api/RouteInfra.hpp"

#include "nvr/api/HttpCommon.hpp"
#include "nvr/api/HttpRouteHelpers.hpp"
#include "nvr/obs/Metrics.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <crow/app.h>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace nvr::api {

void register_infra_health_metrics_branding_routes(
    crow::SimpleApp&          app,
    store::ConfigStore&       store,
    Auth&                     auth,
    CameraSupervisor*        supervisor) {

    CROW_ROUTE(app, "/healthz")([] { return jsonResp(200, {{"status", "ok"}}); });

    CROW_ROUTE(app, "/api/v1/health")
    ([&store, supervisor](const crow::request&) {
        return jsonResp(200,
                         {{"status", "ok"},
                          {"time", isoNow()},
                          {"cameras", supervisor ? supervisor->activeCameras().size() : 0}});
    });

    CROW_ROUTE(app, "/api/v1/health/live")([] { return jsonResp(200, {{"status", "ok"}}); });

    CROW_ROUTE(app, "/api/v1/health/ready")
    ([&store, supervisor](const crow::request&) {
        bool db_ok = true;
        try {
            std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
            SQLite::Statement q(store.db().raw(), "PRAGMA quick_check");
            if (q.executeStep()) db_ok = (q.getColumn(0).getString() == "ok");
        } catch (...) {
            db_ok = false;
        }
        bool has_cam = supervisor && !supervisor->activeCameras().empty();
        return jsonResp(db_ok ? 200 : 503, {{"db", db_ok}, {"cameras", has_cam}});
    });

    CROW_ROUTE(app, "/metrics")
    ([&auth](const crow::request& req) {
        auto ip = clientIp(req);
        const bool loopback =
            (ip == "127.0.0.1" || ip == "::1" || ip == "::ffff:127.0.0.1");
        if (!loopback) {
            auto tok = extractAuthToken(req);
            auto id  = tok ? auth.verify(*tok) : std::nullopt;
            if (!id || !roleAtLeast(id->role, Role::Admin)) return forbidden();
        }
        crow::response r(obs::Registry::instance().renderPrometheus());
        r.set_header("Content-Type", "text/plain; version=0.0.4");
        applySecurityHeaders(r);
        return r;
    });

    CROW_ROUTE(app, "/api/v1/branding")([] {
        json j;
        std::ifstream f("/etc/nvr-prototype/branding.json");
        if (f) f >> j;
        if (j.is_null() || j.is_discarded()) {
            j = {{"product_name", "NVR Prototype"},
                 {"logo_url", "/web/logo.svg"},
                 {"theme_color", "#6366f1"}};
        }
        return jsonResp(200, j);
    });
}

} // namespace nvr::api
