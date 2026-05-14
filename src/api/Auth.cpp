#include "nvr/api/Auth.hpp"

#include "nvr/Logger.hpp"
#include "nvr/store/Crypto.hpp"

#include <jwt-cpp/jwt.h>
#include <sodium.h>

#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace nvr::api {

namespace {

std::string isoUtc(std::chrono::system_clock::time_point tp) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string makeJti() {
    return store::randomHex(16);
}

}

Role parseRole(const std::string& s) {
    if (s == "admin")    return Role::Admin;
    if (s == "operator") return Role::Operator;
    return Role::Viewer;
}

const char* roleToString(Role r) noexcept {
    switch (r) {
        case Role::Admin:    return "admin";
        case Role::Operator: return "operator";
        case Role::Viewer:   return "viewer";
    }
    return "viewer";
}

Auth::Auth(store::Database& db, std::string jwt_secret, store::MasterKey master_key,
           std::chrono::seconds session_access_ttl, std::chrono::seconds session_refresh_ttl)
    : db_(db),
      secret_(std::move(jwt_secret)),
      master_key_(master_key),
      session_access_ttl_(session_access_ttl),
      session_refresh_ttl_(session_refresh_ttl) {}

std::string Auth::loadOrCreateSecret(const std::string& path) {
    fs::path p(path);
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);

    if (fs::exists(p)) {
        std::ifstream in(p);
        std::string secret((std::istreambuf_iterator<char>(in)), {});
        while (!secret.empty() && (secret.back() == '\n' || secret.back() == '\r')) {
            secret.pop_back();
        }
        if (!secret.empty()) return secret;
    }
    std::string secret = store::randomHex(32);
    std::ofstream out(p, std::ios::trunc);
    out << secret;
    out.close();
    fs::permissions(p, fs::perms::owner_read | fs::perms::owner_write);
    NVR_INFO("auth", "jwt secret created at %s", p.c_str());
    return secret;
}

bool Auth::ensureFirstRunAdmin(const std::string& password_file) {
    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    SQLite::Statement q(db_.raw(), "SELECT count(*) FROM users WHERE role='admin' AND disabled=0");
    if (q.executeStep() && q.getColumn(0).getInt() > 0) return false;

    // Generate a strong one-time password and write it to a root-only file.
    // The first-run wizard MUST replace it via /setup or /api/v1/auth/change-password.
    std::string password = store::randomHex(16);  // 128 bits printable

    fs::path pwfile(password_file);
    std::error_code ec;
    fs::create_directories(pwfile.parent_path(), ec);
    std::ofstream out(pwfile, std::ios::trunc);
    if (out) {
        out << password << '\n';
        out.close();
        fs::permissions(pwfile,
            fs::perms::owner_read | fs::perms::owner_write,
            fs::perm_options::replace, ec);
    } else {
        NVR_ERROR("auth", "could not write initial admin password to %s", pwfile.c_str());
    }

    if (!createUser("admin", password, Role::Admin)) {
        // Maybe a disabled admin already exists — rotate its password instead.
        changePassword("admin", password);
        setUserEnabled("admin", true);
    }
    NVR_WARN("auth",
        "first-run: admin password written to %s (chmod 0600). "
        "Change it immediately via /setup or /api/v1/auth/change-password.",
        pwfile.c_str());
    return true;
}

bool Auth::createUser(const std::string& login, const std::string& password, Role role) {
    auto hash = store::hashPassword(password);
    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    try {
        SQLite::Statement q(db_.raw(),
            "INSERT INTO users(login, password_hash, role) VALUES(?,?,?)");
        q.bind(1, login);
        q.bind(2, hash);
        q.bind(3, roleToString(role));
        q.exec();
        db_.audit("system", "user.create", login, "{}");
        return true;
    } catch (const std::exception& e) {
        NVR_WARN("auth", "createUser failed: %s", e.what());
        return false;
    }
}

bool Auth::deleteUser(const std::string& login) {
    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    SQLite::Statement q(db_.raw(), "DELETE FROM users WHERE login=?");
    q.bind(1, login);
    int affected = q.exec();
    if (affected > 0) {
        db_.audit("system", "user.delete", login, "{}");
        revokeAllForUser(login, "deleted");
    }
    return affected > 0;
}

void Auth::listUsers(std::function<void(int64_t, const std::string&, Role, bool, bool)> cb) {
    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    SQLite::Statement q(db_.raw(),
        "SELECT id, login, role, disabled, totp_secret_enc FROM users ORDER BY id");
    while (q.executeStep()) {
        bool has_totp = !q.getColumn(4).isNull();
        cb(q.getColumn(0).getInt64(), q.getColumn(1).getString(),
           parseRole(q.getColumn(2).getString()),
           q.getColumn(3).getInt() != 0, has_totp);
    }
}

bool Auth::setUserEnabled(const std::string& login, bool enabled) {
    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    SQLite::Statement q(db_.raw(),
        "UPDATE users SET disabled=?, token_version=token_version+? WHERE login=?");
    q.bind(1, enabled ? 0 : 1);
    q.bind(2, enabled ? 0 : 1);  // bump token_version on disable
    q.bind(3, login);
    int n = q.exec();
    if (!enabled && n > 0) revokeAllForUser(login, "disabled");
    return n > 0;
}

bool Auth::changePassword(const std::string& login, const std::string& new_password) {
    auto hash = store::hashPassword(new_password);
    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    SQLite::Statement q(db_.raw(),
        "UPDATE users SET password_hash=?, token_version=token_version+1, updated_at=datetime('now') "
        "WHERE login=?");
    q.bind(1, hash);
    q.bind(2, login);
    int n = q.exec();
    if (n > 0) {
        db_.audit(login, "auth.password_change");
        revokeAllForUser(login, "password_change");
    }
    return n > 0;
}

int64_t Auth::tokenVersionFor(const std::string& login) {
    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    SQLite::Statement q(db_.raw(), "SELECT token_version FROM users WHERE login=?");
    q.bind(1, login);
    if (q.executeStep()) return q.getColumn(0).getInt64();
    return 0;
}

namespace {

uint32_t totp_generate(const std::vector<uint8_t>& key, uint64_t counter) {
    unsigned char msg[8];
    for (int i = 7; i >= 0; --i) {
        msg[i] = counter & 0xff;
        counter >>= 8;
    }
    unsigned char hmac[20];
    crypto_auth_hmacsha1(hmac, msg, sizeof(msg), key.data());
    int offset = hmac[19] & 0x0f;
    uint32_t bin = (static_cast<uint32_t>(hmac[offset] & 0x7f) << 24) |
                   (static_cast<uint32_t>(hmac[offset + 1]) << 16) |
                   (static_cast<uint32_t>(hmac[offset + 2]) << 8) |
                   (static_cast<uint32_t>(hmac[offset + 3]));
    return bin % 1000000;
}

std::string base32_encode(const std::vector<uint8_t>& in) {
    static const char alpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    std::string out;
    int buffer = 0, bits_left = 0;
    for (auto b : in) {
        buffer = (buffer << 8) | b;
        bits_left += 8;
        while (bits_left >= 5) {
            bits_left -= 5;
            out.push_back(alpha[(buffer >> bits_left) & 0x1f]);
        }
    }
    if (bits_left > 0) out.push_back(alpha[(buffer << (5 - bits_left)) & 0x1f]);
    while (out.size() % 8) out.push_back('=');
    return out;
}

bool totp_verify(const std::vector<uint8_t>& key, const std::string& code,
                  uint64_t step_seconds = 30) {
    if (code.size() < 6) return false;
    int64_t code_int = 0;
    try { code_int = std::stoll(code); } catch (...) { return false; }
    uint64_t counter = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() / step_seconds;
    for (int dx = -1; dx <= 1; ++dx) {
        if (static_cast<uint32_t>(code_int) == totp_generate(key, counter + dx)) return true;
    }
    return false;
}

}

// AEAD header on encrypted TOTP secrets: legacy rows (pre-master-key) are stored as 20 raw bytes.
// AEAD blobs from store::encrypt() are always >= 28 bytes (24-byte nonce + 16-byte tag at minimum).
static constexpr size_t kRawTotpLegacySize = 20;

std::vector<uint8_t> Auth::getActiveTotpSecret(const std::string& login) {
    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    SQLite::Statement q(db_.raw(), "SELECT totp_secret_enc FROM users WHERE login=?");
    q.bind(1, login);
    if (!q.executeStep() || q.getColumn(0).isNull()) return {};
    auto blob = q.getColumn(0);
    std::vector<uint8_t> bytes(static_cast<const uint8_t*>(blob.getBlob()),
                                static_cast<const uint8_t*>(blob.getBlob()) + blob.getBytes());
    if (bytes.size() == kRawTotpLegacySize) {
        // Migrate legacy plain blob to AEAD-encrypted in place.
        auto enc = store::encrypt(master_key_,
                                  std::string(bytes.begin(), bytes.end()),
                                  "totp:" + login);
        SQLite::Statement u(db_.raw(),
            "UPDATE users SET totp_secret_enc=? WHERE login=?");
        u.bind(1, enc.data(), static_cast<int>(enc.size()));
        u.bind(2, login);
        u.exec();
        return bytes;
    }
    auto plain = store::decrypt(master_key_, bytes, "totp:" + login);
    if (!plain) return {};
    return std::vector<uint8_t>(plain->begin(), plain->end());
}

std::vector<uint8_t> Auth::getPendingTotpSecret(const std::string& login) {
    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    SQLite::Statement q(db_.raw(), "SELECT totp_pending_enc FROM users WHERE login=?");
    q.bind(1, login);
    if (!q.executeStep() || q.getColumn(0).isNull()) return {};
    auto blob = q.getColumn(0);
    std::vector<uint8_t> bytes(static_cast<const uint8_t*>(blob.getBlob()),
                                static_cast<const uint8_t*>(blob.getBlob()) + blob.getBytes());
    auto plain = store::decrypt(master_key_, bytes, "totp_pending:" + login);
    if (!plain) return {};
    return std::vector<uint8_t>(plain->begin(), plain->end());
}

std::string Auth::totpSetupSecret(const std::string& login) {
    std::vector<uint8_t> raw(20);
    randombytes_buf(raw.data(), raw.size());
    auto enc = store::encrypt(master_key_,
                              std::string(raw.begin(), raw.end()),
                              "totp_pending:" + login);
    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    SQLite::Statement q(db_.raw(),
        "UPDATE users SET totp_pending_enc=? WHERE login=?");
    q.bind(1, enc.data(), static_cast<int>(enc.size()));
    q.bind(2, login);
    q.exec();
    return base32_encode(raw);
}

bool Auth::totpActivate(const std::string& login, const std::string& code) {
    auto raw = getPendingTotpSecret(login);
    if (raw.empty()) return false;
    if (!totp_verify(raw, code)) return false;
    auto enc = store::encrypt(master_key_,
                              std::string(raw.begin(), raw.end()),
                              "totp:" + login);
    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    SQLite::Statement q(db_.raw(),
        "UPDATE users SET totp_secret_enc=?, totp_pending_enc=NULL WHERE login=?");
    q.bind(1, enc.data(), static_cast<int>(enc.size()));
    q.bind(2, login);
    q.exec();
    db_.audit(login, "auth.totp_activate");
    return true;
}

bool Auth::totpDisable(const std::string& login) {
    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    SQLite::Statement q(db_.raw(),
        "UPDATE users SET totp_secret_enc=NULL, totp_pending_enc=NULL WHERE login=?");
    q.bind(1, login);
    int n = q.exec();
    if (n > 0) db_.audit(login, "auth.totp_disable");
    return n > 0;
}

std::optional<Identity> Auth::login(const std::string& login, const std::string& password,
                                     const std::string& totp_code) {
    int64_t user_id = 0;
    Role role = Role::Viewer;
    int64_t token_version = 1;
    bool has_totp = false;
    {
        std::lock_guard<std::recursive_mutex> lk(db_.mutex());
        SQLite::Statement q(db_.raw(),
            "SELECT id, password_hash, role, disabled, totp_secret_enc, token_version "
            "FROM users WHERE login=?");
        q.bind(1, login);
        if (!q.executeStep()) return std::nullopt;
        if (q.getColumn(3).getInt() != 0) return std::nullopt;

        auto hash = q.getColumn(1).getString();
        if (!store::verifyPassword(password, hash)) return std::nullopt;

        user_id = q.getColumn(0).getInt64();
        role    = parseRole(q.getColumn(2).getString());
        has_totp = !q.getColumn(4).isNull();
        token_version = q.getColumn(5).getInt64();
    }

    if (has_totp) {
        if (totp_code.empty()) return std::nullopt;
        auto raw = getActiveTotpSecret(login);
        if (raw.empty() || !totp_verify(raw, totp_code)) return std::nullopt;
    }

    Identity id;
    id.user_id       = user_id;
    id.login         = login;
    id.role          = role;
    id.token_version = token_version;
    db_.audit(login, "auth.login");
    return id;
}

std::string Auth::issueToken(const Identity& id, std::chrono::seconds ttl, const std::string& scope) {
    auto jti = makeJti();
    auto builder = jwt::create()
        .set_issuer("nvr-prototype")
        .set_type("JWT")
        .set_id(jti)
        .set_payload_claim("sub",   jwt::claim(id.login))
        .set_payload_claim("uid",   jwt::claim(picojson::value(int64_t(id.user_id))))
        .set_payload_claim("role",  jwt::claim(std::string(roleToString(id.role))))
        .set_payload_claim("tv",    jwt::claim(picojson::value(int64_t(id.token_version))))
        .set_issued_at(std::chrono::system_clock::now())
        .set_expires_at(std::chrono::system_clock::now() + ttl);
    if (!scope.empty()) builder = builder.set_payload_claim("scope", jwt::claim(scope));
    return builder.sign(jwt::algorithm::hs256{secret_});
}

IssuedTokens Auth::issueSession(const Identity& id,
                                 std::chrono::seconds access_ttl,
                                 std::chrono::seconds refresh_ttl) {
    IssuedTokens out;
    Identity ident = id;
    ident.token_version = tokenVersionFor(id.login);

    out.access  = issueToken(ident, access_ttl, "session");
    out.access_ttl_seconds  = static_cast<int>(access_ttl.count());

    auto rjti = makeJti();
    auto exp  = std::chrono::system_clock::now() + refresh_ttl;
    out.refresh = jwt::create()
        .set_issuer("nvr-prototype")
        .set_type("REFRESH")
        .set_id(rjti)
        .set_payload_claim("sub", jwt::claim(ident.login))
        .set_payload_claim("tv",  jwt::claim(picojson::value(int64_t(ident.token_version))))
        .set_issued_at(std::chrono::system_clock::now())
        .set_expires_at(exp)
        .sign(jwt::algorithm::hs256{secret_});
    out.refresh_ttl_seconds = static_cast<int>(refresh_ttl.count());

    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    SQLite::Statement q(db_.raw(),
        "INSERT INTO refresh_tokens(jti, user_login, expires_at) VALUES(?,?,?)");
    q.bind(1, rjti);
    q.bind(2, ident.login);
    q.bind(3, isoUtc(exp));
    q.exec();
    return out;
}

std::optional<IssuedTokens> Auth::refresh(const std::string& refresh_token) {
    try {
        auto decoded = jwt::decode(refresh_token);
        jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{secret_})
            .with_issuer("nvr-prototype")
            .verify(decoded);
        auto type = decoded.get_type();
        if (type != "REFRESH") return std::nullopt;
        auto sub = decoded.get_payload_claim("sub").as_string();
        auto jti = decoded.get_id();
        int64_t tv = decoded.get_payload_claim("tv").as_int();

        // Token must exist and be active.
        std::lock_guard<std::recursive_mutex> lk(db_.mutex());
        SQLite::Statement q(db_.raw(),
            "SELECT revoked FROM refresh_tokens WHERE jti=? AND user_login=?");
        q.bind(1, jti);
        q.bind(2, sub);
        if (!q.executeStep()) return std::nullopt;
        if (q.getColumn(0).getInt() != 0) return std::nullopt;

        // User must still exist, be enabled, and have matching token_version.
        SQLite::Statement uq(db_.raw(),
            "SELECT id, role, disabled, token_version FROM users WHERE login=?");
        uq.bind(1, sub);
        if (!uq.executeStep()) return std::nullopt;
        if (uq.getColumn(2).getInt() != 0) return std::nullopt;
        if (uq.getColumn(3).getInt64() != tv) return std::nullopt;

        // Rotate: revoke old jti and issue fresh pair.
        SQLite::Statement up(db_.raw(),
            "UPDATE refresh_tokens SET revoked=1 WHERE jti=?");
        up.bind(1, jti);
        up.exec();

        Identity id;
        id.user_id       = uq.getColumn(0).getInt64();
        id.login         = sub;
        id.role          = parseRole(uq.getColumn(1).getString());
        id.token_version = tv;
        return issueSession(id, session_access_ttl_, session_refresh_ttl_);
    } catch (const std::exception& e) {
        NVR_TRACE("auth", "refresh failed: %s", e.what());
        return std::nullopt;
    }
}

bool Auth::revokeRefresh(const std::string& refresh_token, const std::string& reason) {
    try {
        auto decoded = jwt::decode(refresh_token);
        jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{secret_})
            .with_issuer("nvr-prototype")
            .verify(decoded);
        auto jti = decoded.get_id();
        auto sub = decoded.get_payload_claim("sub").as_string();
        std::lock_guard<std::recursive_mutex> lk(db_.mutex());
        SQLite::Statement q(db_.raw(),
            "UPDATE refresh_tokens SET revoked=1 WHERE jti=?");
        q.bind(1, jti);
        bool ok = q.exec() > 0;
        if (ok) db_.audit(sub, "auth.logout", sub, std::string("{\"reason\":\"") + reason + "\"}");
        return ok;
    } catch (...) {
        return false;
    }
}

void Auth::revokeAllForUser(const std::string& login, const std::string& reason) {
    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    SQLite::Statement q(db_.raw(),
        "UPDATE refresh_tokens SET revoked=1 WHERE user_login=? AND revoked=0");
    q.bind(1, login);
    q.exec();
    db_.audit("system", "auth.revoke_all", login, std::string("{\"reason\":\"") + reason + "\"}");
}

bool Auth::isRevoked(const std::string& jti) {
    if (jti.empty()) return false;
    std::lock_guard<std::recursive_mutex> lk(db_.mutex());
    SQLite::Statement q(db_.raw(),
        "SELECT 1 FROM token_revocations WHERE jti=? AND expires_at > datetime('now')");
    q.bind(1, jti);
    return q.executeStep();
}

std::optional<Identity> Auth::verify(const std::string& token) {
    try {
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{secret_})
            .with_issuer("nvr-prototype");
        verifier.verify(decoded);

        Identity id;
        id.login = decoded.get_payload_claim("sub").as_string();
        id.role  = parseRole(decoded.get_payload_claim("role").as_string());
        try { id.user_id        = decoded.get_payload_claim("uid").as_int(); } catch (...) {}
        try { id.token_version  = decoded.get_payload_claim("tv").as_int(); }  catch (...) { id.token_version = 1; }
        try { id.scope          = decoded.get_payload_claim("scope").as_string(); } catch (...) {}
        try { id.jti            = decoded.get_id(); } catch (...) {}

        if (isRevoked(id.jti)) return std::nullopt;

        int64_t current_tv = tokenVersionFor(id.login);
        if (current_tv == 0) return std::nullopt;       // user gone
        if (id.token_version != current_tv) return std::nullopt;  // revoked by version bump

        // Reject tokens for disabled users.
        std::lock_guard<std::recursive_mutex> lk(db_.mutex());
        SQLite::Statement q(db_.raw(), "SELECT disabled FROM users WHERE login=?");
        q.bind(1, id.login);
        if (!q.executeStep()) return std::nullopt;
        if (q.getColumn(0).getInt() != 0) return std::nullopt;

        return id;
    } catch (const std::exception& e) {
        NVR_TRACE("auth", "verify failed: %s", e.what());
        return std::nullopt;
    }
}

}
