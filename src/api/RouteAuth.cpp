#include "nvr/api/RouteAuth.hpp"

#include "nvr/api/HttpCommon.hpp"
#include "nvr/api/HttpRouteHelpers.hpp"

#include <crow/app.h>
#include <sstream>
#include <string_view>

namespace nvr::api {

void register_auth_routes(
    crow::SimpleApp&                                                    app,
    Auth&                                                               auth,
    store::ConfigStore&                                                 store,
    std::function<std::optional<Identity>(const crow::request&, Role)> require_auth,
    std::function<bool(const std::string& ip)>                         rate_limit_login) {

    using json = nlohmann::json;

    CROW_ROUTE(app, "/api/v1/auth/login").methods(crow::HTTPMethod::POST)
    ([&auth, &rate_limit_login](const crow::request& req) {
        auto ip = clientIp(req);
        if (!rate_limit_login(ip)) return jsonResp(429, {{"error", "too_many_attempts"}});
        try {
            auto body = json::parse(req.body);
            auto id   = auth.login(body.value("login", ""), body.value("password", ""),
                                    body.value("totp_code", ""));
            if (!id) return unauthorized();
            auto tokens = auth.issueSession(*id);
            json out = {
                {"token",        tokens.access},
                {"access_token", tokens.access},
                {"expires_in",   tokens.access_ttl_seconds},
                {"role",         roleToString(id->role)},
                {"login",        id->login},
            };
            if (jsonRefreshTokenOptIn(req)) out["refresh_token"] = tokens.refresh;
            crow::response r(200, out.dump());
            r.set_header("Content-Type", "application/json");
            std::ostringstream c1;
            c1 << "nvr_refresh=" << tokens.refresh
               << "; Path=/api/v1/auth"
               << "; Max-Age=" << tokens.refresh_ttl_seconds
               << "; HttpOnly; SameSite=Strict; Secure";
            r.add_header("Set-Cookie", c1.str());
            std::ostringstream c2;
            c2 << "nvr_token=" << tokens.access
               << "; Path=/"
               << "; Max-Age=" << tokens.access_ttl_seconds
               << "; HttpOnly; SameSite=Strict; Secure";
            r.add_header("Set-Cookie", c2.str());
            applySecurityHeaders(r);
            return r;
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/auth/refresh").methods(crow::HTTPMethod::POST)
    ([&auth](const crow::request& req) {
        std::string refresh;
        try {
            if (!req.body.empty()) {
                auto body = json::parse(req.body, nullptr, false);
                if (!body.is_discarded()) refresh = body.value("refresh_token", "");
            }
        } catch (...) {}
        if (refresh.empty()) {
            auto cookies = req.get_header_value("Cookie");
            constexpr std::string_view kName = "nvr_refresh=";
            auto pos = cookies.find(kName);
            if (pos != std::string::npos) {
                pos += kName.size();
                auto end = cookies.find(';', pos);
                refresh = cookies.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            }
        }
        if (refresh.empty()) return unauthorized();
        auto tokens = auth.refresh(refresh);
        if (!tokens) return unauthorized();
        json body = {
            {"token",        tokens->access},
            {"access_token", tokens->access},
            {"expires_in",   tokens->access_ttl_seconds},
        };
        if (jsonRefreshTokenOptIn(req)) body["refresh_token"] = tokens->refresh;
        crow::response r(200, body.dump());
        r.set_header("Content-Type", "application/json");
        std::ostringstream c1;
        c1 << "nvr_refresh=" << tokens->refresh
           << "; Path=/api/v1/auth"
           << "; Max-Age=" << tokens->refresh_ttl_seconds
           << "; HttpOnly; SameSite=Strict; Secure";
        r.add_header("Set-Cookie", c1.str());
        std::ostringstream c2;
        c2 << "nvr_token=" << tokens->access
           << "; Path=/"
           << "; Max-Age=" << tokens->access_ttl_seconds
           << "; HttpOnly; SameSite=Strict; Secure";
        r.add_header("Set-Cookie", c2.str());
        applySecurityHeaders(r);
        return r;
    });

    CROW_ROUTE(app, "/api/v1/auth/logout").methods(crow::HTTPMethod::POST)
    ([&auth](const crow::request& req) {
        std::string refresh;
        try {
            auto body = json::parse(req.body, nullptr, false);
            if (!body.is_discarded()) refresh = body.value("refresh_token", "");
        } catch (...) {}
        if (refresh.empty()) {
            auto cookies = req.get_header_value("Cookie");
            constexpr std::string_view kName = "nvr_refresh=";
            auto pos = cookies.find(kName);
            if (pos != std::string::npos) {
                pos += kName.size();
                auto end = cookies.find(';', pos);
                refresh = cookies.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            }
        }
        if (!refresh.empty()) auth.revokeRefresh(refresh, "logout");
        crow::response r(200, "{\"ok\":true}");
        r.set_header("Content-Type", "application/json");
        r.add_header("Set-Cookie",
                     "nvr_refresh=; Path=/api/v1/auth; Max-Age=0; HttpOnly; SameSite=Strict; Secure");
        r.add_header("Set-Cookie", "nvr_token=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict; Secure");
        applySecurityHeaders(r);
        return r;
    });

    CROW_ROUTE(app, "/api/v1/auth/me")
    ([&require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        return jsonResp(200, {{"login", who->login}, {"role", roleToString(who->role)}});
    });

    CROW_ROUTE(app, "/api/v1/auth/change-password").methods(crow::HTTPMethod::POST)
    ([&auth, &store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        try {
            auto body   = json::parse(req.body);
            auto old_pw = body.value("old_password", "");
            auto new_pw = body.value("new_password", "");
            if (new_pw.size() < 8) return jsonResp(400, {{"error", "password_too_short"}});
            if (!auth.login(who->login, old_pw)) return unauthorized();
            if (!auth.changePassword(who->login, new_pw))
                return jsonResp(500, {{"error", "change_failed"}});
            store.db().audit(who->login, "auth.password_change", who->login, "{}");
            return jsonResp(200, {{"ok", true}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/auth/totp/setup").methods(crow::HTTPMethod::POST)
    ([&auth, &store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        auto secret = auth.totpSetupSecret(who->login);
        std::string otpauth =
            "otpauth://totp/NVR:" + who->login + "?secret=" + secret + "&issuer=NVR";
        store.db().audit(who->login, "auth.totp_setup", who->login, "{}");
        return jsonResp(200, {{"secret", secret}, {"otpauth_url", otpauth}});
    });

    CROW_ROUTE(app, "/api/v1/auth/totp/verify").methods(crow::HTTPMethod::POST)
    ([&auth, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        try {
            auto body = json::parse(req.body);
            bool ok   = auth.totpActivate(who->login, body.value("code", ""));
            return jsonResp(ok ? 200 : 400, {{"ok", ok}});
        } catch (const std::exception& e) {
            return jsonResp(400, {{"error", e.what()}});
        }
    });

    CROW_ROUTE(app, "/api/v1/auth/totp/disable").methods(crow::HTTPMethod::POST)
    ([&auth, &store, &require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        bool ok = auth.totpDisable(who->login);
        store.db().audit(who->login, "auth.totp_disable", who->login, "{}");
        return jsonResp(200, {{"ok", ok}});
    });

    CROW_ROUTE(app, "/api/v1/auth/external")
    ([&require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        return jsonResp(200,
                         {{"oidc", {{"enabled", false}, {"issuer", json()}, {"client_id", json()}}},
                          {"ldap", {{"enabled", false}, {"uri", json()}, {"base_dn", json()}}},
                          {"note", "Planned: LDAP bind + OIDC code flow; see docs/SECURITY_EXTERNAL_AUTH.md"}});
    });
}

} // namespace nvr::api
