#include "nvr/api/RouteLiveLayouts.hpp"

#include "nvr/api/HttpCommon.hpp"
#include "nvr/api/HttpRouteHelpers.hpp"

#include "nvr/Config.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace nvr::api {

namespace {

json archiveRootsHealthJson(const ArchiveConfig& ac) {
    json arr = json::array();
    std::vector<std::string> roots = {ac.root_path};
    for (const auto& e : ac.extra_archive_roots) {
        if (!e.empty()) roots.push_back(e);
    }
    for (const auto& r : roots) {
        if (r.empty()) continue;
        json             row;
        std::error_code ec;
        const fs::path p(r);
        const bool     exists = fs::exists(p, ec);
        row["path"]             = r;
        row["exists"]           = exists;
        uintmax_t free_b = 0, cap_b = 0;
        if (exists) {
            auto sp = fs::space(p, ec);
            if (!ec) {
                free_b = sp.available;
                cap_b  = sp.capacity;
            }
        }
        row["free_bytes"]     = free_b;
        row["capacity_bytes"] = cap_b;
        bool writable = false;
        if (exists) {
            try {
                const auto    probe = p / ".nvr_write_probe";
                std::ofstream o(probe, std::ios::binary | std::ios::trunc);
                if (o) {
                    o.put('1');
                    o.flush();
                    writable = o.good();
                }
                o.close();
                fs::remove(probe, ec);
            } catch (...) {
                writable = false;
            }
        }
        row["writable"] = writable;
        arr.push_back(std::move(row));
    }
    return arr;
}

} // namespace

void register_live_layout_and_field_routes(
    crow::SimpleApp&                                                    app,
    store::ConfigStore&                                                 store,
    CameraSupervisor*                                                   supervisor,
    std::function<std::optional<Identity>(const crow::request&, Role)> require_auth) {

    CROW_ROUTE(app, "/api/v1/live-layouts")
    ([&store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        try {
            const std::string raw = store.db().listLiveLayoutsJson(who->login);
            auto              j   = json::parse(raw, nullptr, false);
            if (j.is_discarded()) return jsonResp(500, {{"error", "layout_list_corrupt"}});
            return jsonResp(200, j);
        } catch (const std::exception& e) {
            return jsonResp(500, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/live-layouts").methods(crow::HTTPMethod::POST)
    ([&store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        try {
            auto body = json::parse(req.body);
            const std::string name = body.value("name", "");
            if (name.empty() || name.size() > 128)
                return jsonResp(400, {{"error", "bad_layout_name"}});
            if (!body.contains("payload") || !body["payload"].is_object())
                return jsonResp(400, {{"error", "payload_object_required"}});
            const std::string pj = body["payload"].dump();
            if (pj.size() > 32000) return jsonResp(400, {{"error", "payload_too_large"}});
            store.db().upsertLiveLayout(who->login, name, pj);
            store.db().audit(who->login, "live_layout.upsert", name, "{}");
            return jsonResp(200, {{"ok", true}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/live-layouts/<string>").methods(crow::HTTPMethod::DELETE)
    ([&store, &require_auth](const crow::request& req, const std::string& name) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        if (name.empty() || name.size() > 128) return jsonResp(400, {{"error", "bad_layout_name"}});
        const bool ok = store.db().deleteLiveLayout(who->login, name);
        if (ok) store.db().audit(who->login, "live_layout.delete", name, "{}");
        return jsonResp(ok ? 200 : 404, {{"ok", ok}});
    });

    CROW_ROUTE(app, "/api/v1/system/field-bundle")
    ([&store, supervisor, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        const ArchiveConfig ac = store.archiveConfig();
        json                out;
        out["time"]              = isoNow();
        out["telemetry_opt_in"]  = store.db().getSetting("telemetry.opt_in", "false") == "true";
        out["archive_roots"]     = archiveRootsHealthJson(ac);
        out["active_cameras"]    = supervisor ? static_cast<int>(supervisor->activeCameras().size()) : 0;
        int cam_count = 0;
        {
            std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
            SQLite::Statement q(store.db().raw(), "SELECT COUNT(*) FROM cameras WHERE disabled=0");
            if (q.executeStep()) cam_count = q.getColumn(0).getInt();
        }
        out["configured_cameras"] = cam_count;
        store.db().audit(who->login, "system.field_bundle", "", "{}");
        return jsonResp(200, out);
    });
}

} // namespace nvr::api
