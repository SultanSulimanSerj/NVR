#include "nvr/api/RouteConfig.hpp"

#include "nvr/api/HttpCommon.hpp"
#include "nvr/api/HttpRouteHelpers.hpp"

#include <crow/app.h>

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace nvr::api {

void register_config_motion_archive_routes(
    crow::SimpleApp&                                                    app,
    store::ConfigStore&                                                 store,
    std::function<std::optional<Identity>(const crow::request&, Role)> require_auth) {

    CROW_ROUTE(app, "/api/v1/config/motion").methods(crow::HTTPMethod::GET)
    ([&store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        auto m = store.motionConfig();
        return jsonResp(200,
                         {{"downscale_width", m.downscale_width},
                          {"downscale_height", m.downscale_height},
                          {"history", m.history},
                          {"var_threshold", m.var_threshold},
                          {"detect_shadows", m.detect_shadows},
                          {"min_area_ratio", m.min_area_ratio},
                          {"cooldown_seconds", m.cooldown_seconds},
                          {"snapshot_dir", m.snapshot_dir},
                          {"snapshot_jpeg_quality", m.snapshot_jpeg_quality}});
    });

    CROW_ROUTE(app, "/api/v1/config/motion").methods(crow::HTTPMethod::PUT)
    ([&store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        try {
            auto b = json::parse(req.body);
            auto m = store.motionConfig();
            m.downscale_width       = b.value("downscale_width", m.downscale_width);
            m.downscale_height      = b.value("downscale_height", m.downscale_height);
            m.history               = b.value("history", m.history);
            m.var_threshold         = b.value("var_threshold", m.var_threshold);
            m.detect_shadows        = b.value("detect_shadows", m.detect_shadows);
            m.min_area_ratio        = b.value("min_area_ratio", m.min_area_ratio);
            m.cooldown_seconds      = b.value("cooldown_seconds", m.cooldown_seconds);
            m.snapshot_dir          = b.value("snapshot_dir", m.snapshot_dir);
            m.snapshot_jpeg_quality = b.value("snapshot_jpeg_quality", m.snapshot_jpeg_quality);
            store.setMotionConfig(m);
            store.db().audit(who->login, "config.motion.update", "", req.body);
            return jsonResp(200, {{"ok", true}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/config/archive").methods(crow::HTTPMethod::GET)
    ([&store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        auto a = store.archiveConfig();
        return jsonResp(200,
                         {{"root_path", a.root_path},
                          {"segment_minutes", a.segment_minutes},
                          {"target_usage_ratio", a.target_usage_ratio},
                          {"release_to_ratio", a.release_to_ratio},
                          {"min_keep_minutes", a.min_keep_minutes},
                          {"file_prefix", a.file_prefix},
                          {"file_extension", a.file_extension},
                          {"export_watermark_text", a.export_watermark_text},
                          {"extra_archive_roots", a.extra_archive_roots}});
    });

    CROW_ROUTE(app, "/api/v1/config/archive").methods(crow::HTTPMethod::PUT)
    ([&store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        try {
            auto b = json::parse(req.body);
            auto a = store.archiveConfig();
            a.root_path          = b.value("root_path", a.root_path);
            a.segment_minutes    = b.value("segment_minutes", a.segment_minutes);
            a.target_usage_ratio = b.value("target_usage_ratio", a.target_usage_ratio);
            a.release_to_ratio   = b.value("release_to_ratio", a.release_to_ratio);
            a.min_keep_minutes   = b.value("min_keep_minutes", a.min_keep_minutes);
            a.file_prefix        = b.value("file_prefix", a.file_prefix);
            a.file_extension     = b.value("file_extension", a.file_extension);
            a.export_watermark_text = b.value("export_watermark_text", a.export_watermark_text);
            if (b.contains("extra_archive_roots") && b["extra_archive_roots"].is_array()) {
                a.extra_archive_roots.clear();
                for (const auto& e : b["extra_archive_roots"]) {
                    if (e.is_string()) a.extra_archive_roots.push_back(e.get<std::string>());
                }
            }
            store.setArchiveConfig(a);
            store.db().audit(who->login, "config.archive.update", "", req.body);
            return jsonResp(200, {{"ok", true}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/config/features").methods(crow::HTTPMethod::GET)
    ([&store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        return jsonResp(200,
                         {{"mqtt_delivery", store.db().getSetting("features.mqtt_delivery", "stub")}});
    });

    CROW_ROUTE(app, "/api/v1/config/features").methods(crow::HTTPMethod::PUT)
    ([&store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        try {
            auto b = json::parse(req.body);
            if (!b.contains("mqtt_delivery") || !b["mqtt_delivery"].is_string())
                return jsonResp(400, {{"error", "mqtt_delivery_string_required"}});
            const std::string v = b["mqtt_delivery"].get<std::string>();
            if (v != "stub" && v != "live")
                return jsonResp(400, {{"error", "mqtt_delivery_invalid"}, {"allowed", json::array({"stub", "live"})}});
            store.db().setSetting("features.mqtt_delivery", v);
            store.db().audit(who->login, "config.features.update", "", req.body);
            return jsonResp(200, {{"ok", true}, {"mqtt_delivery", v}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/config/map").methods(crow::HTTPMethod::GET)
    ([&store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        return jsonResp(200, {{"plan_image_url", store.db().getSetting("map.plan_image_url", "")}});
    });

    CROW_ROUTE(app, "/api/v1/config/map").methods(crow::HTTPMethod::PUT)
    ([&store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        try {
            auto b = json::parse(req.body);
            if (!b.contains("plan_image_url") || !b["plan_image_url"].is_string())
                return jsonResp(400, {{"error", "plan_image_url_string_required"}});
            const std::string u = b["plan_image_url"].get<std::string>();
            if (u.size() > 2048) return jsonResp(400, {{"error", "plan_image_url_too_long"}});
            store.db().setSetting("map.plan_image_url", u);
            store.db().audit(who->login, "config.map.update", "", req.body);
            return jsonResp(200, {{"ok", true}, {"plan_image_url", u}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/system/camera-catalog").methods(crow::HTTPMethod::GET)
    ([&require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        const char*       ev = std::getenv("NVR_CAMERA_CATALOG_PATH");
        const fs::path    path =
            (ev && ev[0]) ? fs::path(ev) : fs::path("/etc/nvr-prototype/camera_catalog.json");
        json        items = json::array();
        bool        loaded = false;
        std::string err;
        if (isRegularFileNoSymlink(path)) {
            std::string raw;
            if (readFile(path, raw)) {
                auto j = json::parse(raw, nullptr, false);
                if (j.is_array()) {
                    items  = std::move(j);
                    loaded = true;
                } else
                    err = "catalog_must_be_json_array";
            } else
                err = "read_failed";
        }
        json body{{"items", std::move(items)}, {"source", path.string()}, {"loaded", loaded}};
        if (!err.empty()) body["error"] = err;
        return jsonResp(200, body);
    });
}

} // namespace nvr::api
