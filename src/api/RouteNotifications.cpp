#include "nvr/api/RouteNotifications.hpp"

#include "nvr/api/HttpCommon.hpp"
#include "nvr/api/HttpRouteHelpers.hpp"
#include "nvr/EventBus.hpp"
#include "nvr/notify/NotificationManager.hpp"
#include "nvr/store/Crypto.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <crow/app.h>
#include <set>

using json = nlohmann::json;

namespace nvr::api {

namespace {

json channelsList(store::ConfigStore& store) {
    std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
    SQLite::Statement q(store.db().raw(),
                        "SELECT id, kind, name, enabled FROM notification_channels ORDER BY id");
    json out = json::array();
    while (q.executeStep()) {
        out.push_back({{"id", q.getColumn(0).getInt64()},
                        {"kind", q.getColumn(1).getString()},
                        {"name", q.getColumn(2).getString()},
                        {"enabled", q.getColumn(3).getInt() != 0}});
    }
    return out;
}

json rulesList(store::ConfigStore& store) {
    std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
    SQLite::Statement q(store.db().raw(),
                        "SELECT id, camera_id, event_type, severity_min, throttle_seconds, channel_id "
                        "FROM notification_rules ORDER BY id");
    json out = json::array();
    while (q.executeStep()) {
        out.push_back({{"id", q.getColumn(0).getInt64()},
                        {"camera_id", q.getColumn(1).isNull() ? "" : q.getColumn(1).getString()},
                        {"event_type", q.getColumn(2).getString()},
                        {"severity_min", q.getColumn(3).getString()},
                        {"throttle_seconds", q.getColumn(4).getInt()},
                        {"channel_id", q.getColumn(5).getInt64()}});
    }
    return out;
}

} // namespace

void register_notification_routes(
    crow::SimpleApp&                                                    app,
    store::ConfigStore&                                                 store,
    notify::NotificationManager*                                        notify_mgr,
    EventBus*                                                           event_bus,
    std::function<std::optional<Identity>(const crow::request&, Role)> require_auth) {

    CROW_ROUTE(app, "/api/v1/notifications/channels").methods(crow::HTTPMethod::GET)
    ([&store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        return jsonResp(200, channelsList(store));
    });

    CROW_ROUTE(app, "/api/v1/notifications/channels/<int>").methods(crow::HTTPMethod::GET)
    ([&store, &require_auth](const crow::request& req, int id) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
        SQLite::Statement q(store.db().raw(),
                            "SELECT id, kind, name, enabled, config_enc FROM notification_channels WHERE id=?");
        q.bind(1, id);
        if (!q.executeStep()) return notFound();
        json cfg = json::object();
        auto blob_col = q.getColumn(4);
        if (!blob_col.isNull()) {
            std::vector<uint8_t> bytes(static_cast<const uint8_t*>(blob_col.getBlob()),
                                       static_cast<const uint8_t*>(blob_col.getBlob()) +
                                           blob_col.getBytes());
            auto plain = store::decrypt(store.masterKey(), bytes, "notif:" + std::to_string(id));
            if (plain) {
                auto parsed = json::parse(*plain, nullptr, false);
                if (!parsed.is_discarded() && parsed.is_object()) {
                    static const std::set<std::string> secrets{"password", "smtp_password", "bot_token",
                                                             "webhook_secret", "api_key", "token",
                                                             "mqtt_password", "mqtt_pass"};
                    for (auto it = parsed.begin(); it != parsed.end(); ++it) {
                        if (secrets.count(it.key())) cfg[it.key()] = "********";
                        else cfg[it.key()] = it.value();
                    }
                }
            }
        }
        return jsonResp(200,
                         {{"id", q.getColumn(0).getInt64()},
                          {"kind", q.getColumn(1).getString()},
                          {"name", q.getColumn(2).getString()},
                          {"enabled", q.getColumn(3).getInt() != 0},
                          {"config", cfg}});
    });

    CROW_ROUTE(app, "/api/v1/notifications/channels").methods(crow::HTTPMethod::POST)
    ([&store, &notify_mgr, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        try {
            auto b     = json::parse(req.body);
            auto kind  = b.value("kind", "");
            auto name  = b.value("name", kind);
            auto cfg   = b.value("config", json::object()).dump();
            bool en    = b.value("enabled", true);
            int64_t id = 0;
            {
                std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
                SQLite::Statement q(store.db().raw(),
                                    "INSERT INTO notification_channels(kind, name, enabled) VALUES(?,?,?)");
                q.bind(1, kind);
                q.bind(2, name);
                q.bind(3, en ? 1 : 0);
                q.exec();
                id = store.db().raw().getLastInsertRowid();
                auto enc = store::encrypt(store.masterKey(), cfg, "notif:" + std::to_string(id));
                SQLite::Statement u(store.db().raw(),
                                    "UPDATE notification_channels SET config_enc=? WHERE id=?");
                u.bind(1, enc.data(), static_cast<int>(enc.size()));
                u.bind(2, id);
                u.exec();
            }
            if (notify_mgr) notify_mgr->reload();
            store.db().audit(who->login, "notify.channel.create", std::to_string(id),
                             json{{"kind", kind}, {"name", name}}.dump());
            return jsonResp(201, {{"id", id}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/notifications/channels/<int>").methods(crow::HTTPMethod::PATCH)
    ([&store, &notify_mgr, &require_auth](const crow::request& req, int id) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        try {
            auto b = json::parse(req.body);
            std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
            if (b.contains("name")) {
                SQLite::Statement q(store.db().raw(),
                                    "UPDATE notification_channels SET name=? WHERE id=?");
                q.bind(1, b["name"].get<std::string>());
                q.bind(2, id);
                q.exec();
            }
            if (b.contains("enabled")) {
                SQLite::Statement q(store.db().raw(),
                                    "UPDATE notification_channels SET enabled=? WHERE id=?");
                q.bind(1, b["enabled"].get<bool>() ? 1 : 0);
                q.bind(2, id);
                q.exec();
            }
            if (b.contains("config") && b["config"].is_object()) {
                json merged = json::object();
                {
                    SQLite::Statement q(store.db().raw(),
                                        "SELECT config_enc FROM notification_channels WHERE id=?");
                    q.bind(1, id);
                    if (q.executeStep() && !q.getColumn(0).isNull()) {
                        auto col = q.getColumn(0);
                        std::vector<uint8_t> bytes(
                            static_cast<const uint8_t*>(col.getBlob()),
                            static_cast<const uint8_t*>(col.getBlob()) + col.getBytes());
                        auto plain =
                            store::decrypt(store.masterKey(), bytes, "notif:" + std::to_string(id));
                        if (plain) {
                            auto parsed = json::parse(*plain, nullptr, false);
                            if (!parsed.is_discarded() && parsed.is_object()) merged = parsed;
                        }
                    }
                }
                static const std::set<std::string> kSecrets{
                    "password", "smtp_password", "bot_token", "webhook_secret",
                    "api_key", "token", "mqtt_password", "mqtt_pass"};
                for (auto it = b["config"].begin(); it != b["config"].end(); ++it) {
                    if (kSecrets.count(it.key())) {
                        if (it.value().is_string()) {
                            auto sv = it.value().get<std::string>();
                            if (sv.empty() || sv == "********") continue;
                        }
                    }
                    merged[it.key()] = it.value();
                }
                auto enc = store::encrypt(store.masterKey(), merged.dump(), "notif:" + std::to_string(id));
                SQLite::Statement q(store.db().raw(),
                                    "UPDATE notification_channels SET config_enc=? WHERE id=?");
                q.bind(1, enc.data(), static_cast<int>(enc.size()));
                q.bind(2, id);
                q.exec();
            }
            if (notify_mgr) notify_mgr->reload();
            json safe = b;
            if (safe.contains("config") && safe["config"].is_object()) {
                for (auto it = safe["config"].begin(); it != safe["config"].end(); ++it) {
                    static const std::set<std::string> kSec{
                        "password", "smtp_password", "bot_token", "webhook_secret",
                        "api_key", "token", "mqtt_password", "mqtt_pass"};
                    if (kSec.count(it.key())) it.value() = "***";
                }
            }
            store.db().audit(who->login, "notify.channel.update", std::to_string(id), safe.dump());
            return jsonResp(200, {{"ok", true}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/notifications/channels/<int>").methods(crow::HTTPMethod::DELETE)
    ([&store, &notify_mgr, &require_auth](const crow::request& req, int id) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        {
            std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
            SQLite::Statement q(store.db().raw(), "DELETE FROM notification_channels WHERE id=?");
            q.bind(1, id);
            q.exec();
        }
        if (notify_mgr) notify_mgr->reload();
        store.db().audit(who->login, "notify.channel.delete", std::to_string(id), "{}");
        return crow::response(204);
    });

    CROW_ROUTE(app, "/api/v1/notifications/channels/<int>/test").methods(crow::HTTPMethod::POST)
    ([&store, &event_bus, &require_auth](const crow::request& req, int id) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        if (!event_bus) return jsonResp(503, {{"error", "no_bus"}});
        SystemEvent ev;
        ev.camera_id    = "system";
        ev.type         = "test_notification";
        ev.severity     = "info";
        ev.payload_json = json{{"channel_id", id}, {"by", who->login}}.dump();
        event_bus->publish(std::move(ev));
        store.db().audit(who->login, "notify.channel.test", std::to_string(id), "{}");
        return jsonResp(200, {{"queued", true}});
    });

    CROW_ROUTE(app, "/api/v1/notifications/rules").methods(crow::HTTPMethod::GET)
    ([&store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        return jsonResp(200, rulesList(store));
    });

    CROW_ROUTE(app, "/api/v1/notifications/rules").methods(crow::HTTPMethod::POST)
    ([&store, &notify_mgr, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        try {
            auto b = json::parse(req.body);
            std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
            SQLite::Statement q(store.db().raw(),
                                "INSERT INTO notification_rules(camera_id, event_type, severity_min, "
                                "throttle_seconds, channel_id) VALUES(?,?,?,?,?)");
            auto cam = b.value("camera_id", "");
            if (cam.empty())
                q.bind(1);
            else
                q.bind(1, cam);
            q.bind(2, b.value("event_type", "*"));
            q.bind(3, b.value("severity_min", "info"));
            q.bind(4, b.value("throttle_seconds", 30));
            q.bind(5, b.value("channel_id", 0));
            q.exec();
            int64_t id = store.db().raw().getLastInsertRowid();
            if (notify_mgr) notify_mgr->reload();
            store.db().audit(who->login, "notify.rule.create", std::to_string(id), req.body);
            return jsonResp(201, {{"id", id}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/notifications/rules/<int>").methods(crow::HTTPMethod::DELETE)
    ([&store, &notify_mgr, &require_auth](const crow::request& req, int id) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        {
            std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
            SQLite::Statement q(store.db().raw(), "DELETE FROM notification_rules WHERE id=?");
            q.bind(1, id);
            q.exec();
        }
        if (notify_mgr) notify_mgr->reload();
        store.db().audit(who->login, "notify.rule.delete", std::to_string(id), "{}");
        return crow::response(204);
    });

    CROW_ROUTE(app, "/api/v1/notifications/dead-letter").methods(crow::HTTPMethod::GET)
    ([&store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        int limit = 200;
        if (auto v = req.url_params.get("limit")) try { limit = std::stoi(v); } catch (...) {}
        limit = clampLimit(limit, 1, kMaxAuditLimit);
        json out = json::array();
        {
            std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
            SQLite::Statement q(store.db().raw(),
                                "SELECT id, ts, channel_id, rule_id, attempts, last_error, payload_json "
                                "FROM notify_dead_letter ORDER BY id DESC LIMIT ?");
            q.bind(1, limit);
            while (q.executeStep()) {
                json pj = json::object();
                if (!q.getColumn(6).isNull()) {
                    try {
                        pj = json::parse(q.getColumn(6).getString());
                    } catch (...) {
                        pj = json::object();
                    }
                }
                out.push_back({{"id", q.getColumn(0).getInt64()},
                               {"ts", q.getColumn(1).getString()},
                               {"channel_id",
                                q.getColumn(2).isNull() ? json(nullptr) : json(q.getColumn(2).getInt64())},
                               {"rule_id",
                                q.getColumn(3).isNull() ? json(nullptr) : json(q.getColumn(3).getInt64())},
                               {"attempts", q.getColumn(4).getInt64()},
                               {"last_error", q.getColumn(5).isNull() ? "" : q.getColumn(5).getString()},
                               {"payload_json", pj}});
            }
        }
        return jsonResp(200, out);
    });
}

} // namespace nvr::api
