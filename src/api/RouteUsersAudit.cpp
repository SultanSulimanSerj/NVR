#include "nvr/api/RouteUsersAudit.hpp"

#include "nvr/api/HttpCommon.hpp"
#include "nvr/api/HttpRouteHelpers.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <crow/app.h>

using json = nlohmann::json;

namespace nvr::api {

void register_users_audit_routes(
    crow::SimpleApp&                                                    app,
    Auth&                                                               auth,
    store::ConfigStore&                                                 store,
    std::function<std::optional<Identity>(const crow::request&, Role)> require_auth) {

    CROW_ROUTE(app, "/api/v1/users").methods(crow::HTTPMethod::GET)
    ([&auth, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        json out = json::array();
        auth.listUsers([&](int64_t id, const std::string& login, Role role, bool disabled,
                           bool has_totp) {
            out.push_back({{"id", id},
                           {"login", login},
                           {"role", roleToString(role)},
                           {"disabled", disabled},
                           {"has_totp", has_totp}});
        });
        return jsonResp(200, out);
    });

    CROW_ROUTE(app, "/api/v1/users").methods(crow::HTTPMethod::POST)
    ([&auth, &store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        try {
            auto body  = json::parse(req.body);
            auto login = body.value("login", "");
            auto pass  = body.value("password", "");
            auto role  = parseRole(body.value("role", "viewer"));
            if (login.empty() || pass.empty()) return jsonResp(400, {{"error", "missing"}});
            if (!auth.createUser(login, pass, role)) return jsonResp(409, {{"error", "exists"}});
            store.db().audit(who->login, "user.create", login,
                             json{{"role", roleToString(role)}}.dump());
            return crow::response(201);
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/users/<string>").methods(crow::HTTPMethod::PATCH)
    ([&auth, &store, &require_auth](const crow::request& req, const std::string& login) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        try {
            auto body = json::parse(req.body);
            if (body.contains("disabled")) {
                auth.setUserEnabled(login, !body["disabled"].get<bool>());
                store.db().audit(who->login, "user.set_enabled", login,
                                 json{{"disabled", body["disabled"].get<bool>()}}.dump());
            }
            if (body.contains("password")) {
                auth.changePassword(login, body["password"].get<std::string>());
                store.db().audit(who->login, "user.reset_password", login, "{}");
            }
            return jsonResp(200, {{"ok", true}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/users/<string>").methods(crow::HTTPMethod::DELETE)
    ([&auth, &store, &require_auth](const crow::request& req, const std::string& login) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        if (!auth.deleteUser(login)) return notFound();
        store.db().audit(who->login, "user.delete", login, "{}");
        return crow::response(204);
    });

    CROW_ROUTE(app, "/api/v1/users/<string>/cameras").methods(crow::HTTPMethod::GET)
    ([&store, &require_auth](const crow::request& req, const std::string& login) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        json arr = json::array();
        std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
        SQLite::Statement q(store.db().raw(),
                            "SELECT camera_id FROM camera_acl WHERE user_login=? ORDER BY camera_id");
        q.bind(1, login);
        while (q.executeStep()) arr.push_back(q.getColumn(0).getString());
        return jsonResp(200, {{"login", login}, {"camera_ids", arr}});
    });

    CROW_ROUTE(app, "/api/v1/users/<string>/cameras").methods(crow::HTTPMethod::PUT)
    ([&store, &require_auth](const crow::request& req, const std::string& login) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return jsonResp(400, {{"error", "bad_json"}});
        }
        if (!body.contains("camera_ids") || !body["camera_ids"].is_array())
            return jsonResp(400, {{"error", "camera_ids_array_required"}});
        std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
        SQLite::Statement del(store.db().raw(), "DELETE FROM camera_acl WHERE user_login=?");
        del.bind(1, login);
        del.exec();
        for (const auto& e : body["camera_ids"]) {
            if (!e.is_string()) continue;
            const std::string cid = e.get<std::string>();
            if (cid.empty() || cid.size() > 96) continue;
            SQLite::Statement ins(store.db().raw(),
                                  "INSERT OR IGNORE INTO camera_acl(user_login, camera_id) VALUES(?,?)");
            ins.bind(1, login);
            ins.bind(2, cid);
            ins.exec();
        }
        store.db().audit(who->login, "user.cameras_acl", login, req.body);
        return jsonResp(200, {{"ok", true}});
    });

    CROW_ROUTE(app, "/api/v1/audit")
    ([&store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        int limit = 200;
        if (auto v = req.url_params.get("limit")) try { limit = std::stoi(v); } catch (...) {}
        limit = clampLimit(limit, 1, kMaxAuditLimit);
        std::string user, action;
        if (auto v = req.url_params.get("user")) user = v;
        if (auto v = req.url_params.get("action")) action = v;

        std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
        std::string sql = "SELECT id, ts, user_login, action, target, payload FROM audit_log WHERE 1=1";
        if (!user.empty()) sql += " AND user_login=?";
        if (!action.empty()) sql += " AND action=?";
        sql += " ORDER BY id DESC LIMIT ?";
        SQLite::Statement q(store.db().raw(), sql);
        int idx = 1;
        if (!user.empty()) q.bind(idx++, user);
        if (!action.empty()) q.bind(idx++, action);
        q.bind(idx++, limit);
        json out = json::array();
        while (q.executeStep()) {
            out.push_back({{"id", q.getColumn(0).getInt64()},
                           {"ts", q.getColumn(1).getString()},
                           {"user", q.getColumn(2).isNull() ? "" : q.getColumn(2).getString()},
                           {"action", q.getColumn(3).getString()},
                           {"target", q.getColumn(4).isNull() ? "" : q.getColumn(4).getString()},
                           {"payload", q.getColumn(5).isNull() ? "" : q.getColumn(5).getString()}});
        }
        return jsonResp(200, out);
    });
}

} // namespace nvr::api
