#include "nvr/api/RouteStream.hpp"

#include "nvr/api/HttpCommon.hpp"
#include "nvr/api/HttpRouteHelpers.hpp"

#include <crow/app.h>

namespace fs = std::filesystem;

namespace nvr::api {

void register_stream_routes(
    crow::SimpleApp&                                                    app,
    const HttpConfig&                                                   cfg,
    store::ConfigStore&                                                 store,
    Auth&                                                               auth,
    std::mutex&                                                         ws_mu,
    std::set<crow::websocket::connection*>&                             ws_conns,
    std::function<std::optional<Identity>(const crow::request&, Role)> require_auth_stream) {

    CROW_ROUTE(app, "/live/<string>/<string>")
    ([&cfg, &store, &require_auth_stream](const crow::request& req, const std::string& cam,
                                          const std::string& file) {
        auto who = require_auth_stream(req, Role::Viewer);
        if (!who) return unauthorized();
        if (!isSafeToken(cam, 64) || !isSafeToken(file, 128)) return notFound();
        if (!store.getCamera(cam)) return notFound();
        if (!cameraVisibleToUser(store, *who, cam)) return forbidden();
        fs::path fpath(file);
        auto     ext = fpath.extension().string();
        if (ext != ".m3u8" && ext != ".m4s" && ext != ".ts" && ext != ".mp4" && ext != ".aac")
            return notFound();
        fs::path p = fs::path(cfg.hls_root) / cam / file;
        if (!pathInsideRoot(p, fs::path(cfg.hls_root)) || !isRegularFileNoSymlink(p))
            return notFound();
        std::string body;
        if (!readFile(p, body)) return notFound();
        crow::response r(body);
        r.set_header("Content-Type", mimeFor(p));
        r.set_header("Cache-Control", p.extension() == ".m3u8" ? "no-cache" : "max-age=2");
        applySecurityHeaders(r);
        return r;
    });

    CROW_WEBSOCKET_ROUTE(app, "/api/v1/ws")
        .onaccept([&auth](const crow::request& req, void**) {
            auto tok = extractStreamToken(req);
            if (!tok) return false;
            return auth.verify(*tok).has_value();
        })
        .onopen([&ws_mu, &ws_conns](crow::websocket::connection& conn) {
            std::lock_guard<std::mutex> lk(ws_mu);
            ws_conns.insert(&conn);
        })
        .onclose([&ws_mu, &ws_conns](crow::websocket::connection& conn, const std::string&) {
            std::lock_guard<std::mutex> lk(ws_mu);
            ws_conns.erase(&conn);
        })
        .onmessage([](crow::websocket::connection& conn, const std::string&, bool) {
            conn.send_text(R"({"ack":true})");
        });
}

} // namespace nvr::api
