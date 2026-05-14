#pragma once

#include "nvr/store/Crypto.hpp"
#include "nvr/store/Database.hpp"

#include <chrono>
#include <optional>
#include <string>

namespace nvr::api {

enum class Role { Admin, Operator, Viewer };

Role        parseRole(const std::string& s);
const char* roleToString(Role r) noexcept;

struct Identity {
    int64_t     user_id{0};
    std::string login;
    Role        role{Role::Viewer};
    int64_t     token_version{1};
    std::string jti;
    std::string scope;
};

struct IssuedTokens {
    std::string access;
    std::string refresh;
    int         access_ttl_seconds{0};
    int         refresh_ttl_seconds{0};
};

class Auth {
public:
    Auth(store::Database& db, std::string jwt_secret, store::MasterKey master_key,
         std::chrono::seconds session_access_ttl  = std::chrono::minutes{15},
         std::chrono::seconds session_refresh_ttl = std::chrono::hours{12});

    // Returns true if a brand-new admin was provisioned (with one-shot password file).
    bool        ensureFirstRunAdmin(const std::string& password_file = "/etc/nvr-prototype/initial-admin.password");

    std::optional<Identity> login(const std::string& login, const std::string& password,
                                   const std::string& totp_code = {});
    std::optional<Identity> verify(const std::string& token);

    // Single token (backwards-compatible path used by long-lived kiosk).
    std::string issueToken(const Identity& id, std::chrono::seconds ttl = std::chrono::hours{8},
                            const std::string& scope = "");
    // Access + refresh pair (preferred for interactive sessions).
    IssuedTokens issueSession(const Identity& id,
                               std::chrono::seconds access_ttl  = std::chrono::minutes{15},
                               std::chrono::seconds refresh_ttl = std::chrono::hours{12});
    std::optional<IssuedTokens> refresh(const std::string& refresh_token);
    bool revokeRefresh(const std::string& refresh_token, const std::string& reason = "logout");
    void revokeAllForUser(const std::string& login, const std::string& reason = "user_disabled");

    bool createUser(const std::string& login, const std::string& password, Role role);
    bool deleteUser(const std::string& login);
    bool setUserEnabled(const std::string& login, bool enabled);
    bool changePassword(const std::string& login, const std::string& new_password);
    void listUsers(std::function<void(int64_t, const std::string&, Role, bool, bool)> cb);

    // Pending vs activated secret:
    //   totpSetupSecret writes to users.totp_pending_enc (no auth impact yet).
    //   totpActivate verifies a code against the pending secret and promotes it to totp_secret_enc.
    std::string totpSetupSecret(const std::string& login);
    bool        totpActivate  (const std::string& login, const std::string& code);
    bool        totpDisable   (const std::string& login);

    // Hooks for HttpServer to surface revoked tokens to user-facing messaging.
    bool isRevoked(const std::string& jti);

    static std::string loadOrCreateSecret(const std::string& path);

    store::Database& db() noexcept { return db_; }

private:
    store::Database&  db_;
    std::string       secret_;
    store::MasterKey  master_key_;
    std::chrono::seconds session_access_ttl_;
    std::chrono::seconds session_refresh_ttl_;

    std::vector<uint8_t> getActiveTotpSecret(const std::string& login);
    std::vector<uint8_t> getPendingTotpSecret(const std::string& login);
    int64_t              tokenVersionFor(const std::string& login);
};

}
