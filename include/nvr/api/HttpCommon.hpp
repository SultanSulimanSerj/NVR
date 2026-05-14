#pragma once

#include <algorithm>
#include <crow.h>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace nvr::api {

inline void applySecurityHeaders(crow::response& r, bool html = false) {
    r.set_header("X-Content-Type-Options", "nosniff");
    r.set_header("X-Frame-Options",        "DENY");
    r.set_header("Referrer-Policy",        "strict-origin-when-cross-origin");
    if (html) {
        r.set_header("Content-Security-Policy",
            "default-src 'self'; "
            "img-src 'self' data: blob:; "
            "media-src 'self' blob:; "
            "connect-src 'self' ws: wss:; "
            "script-src 'self' 'unsafe-inline'; "
            "style-src 'self' 'unsafe-inline'; "
            "frame-ancestors 'none'");
    }
}

inline crow::response jsonResp(int code, const nlohmann::json& body) {
    crow::response r(code, body.dump());
    r.set_header("Content-Type", "application/json");
    applySecurityHeaders(r);
    return r;
}

inline crow::response unauthorized() {
    auto r = jsonResp(401, {{"error", "unauthorized"}});
    r.set_header("WWW-Authenticate", "Bearer");
    return r;
}

inline crow::response forbidden() { return jsonResp(403, {{"error", "forbidden"}}); }
inline crow::response notFound()  { return jsonResp(404, {{"error", "not_found"}}); }

inline bool pathInsideRoot(const std::filesystem::path& candidate, const std::filesystem::path& root) {
    std::error_code ec;
    auto cand_canon = std::filesystem::weakly_canonical(candidate, ec);
    if (ec) return false;
    auto root_canon = std::filesystem::weakly_canonical(root, ec);
    if (ec) return false;
    auto c_str = cand_canon.lexically_normal().string();
    auto r_str = root_canon.lexically_normal().string();
    if (!r_str.empty() && r_str.back() != '/') r_str.push_back('/');
    return c_str.rfind(r_str, 0) == 0 || c_str == root_canon.lexically_normal().string();
}

inline bool isRegularFileNoSymlink(const std::filesystem::path& p) {
    std::error_code ec;
    auto            st = std::filesystem::symlink_status(p, ec);
    if (ec) return false;
    if (std::filesystem::is_symlink(st)) return false;
    return std::filesystem::is_regular_file(p, ec);
}

inline int clampLimit(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

inline constexpr int kMaxEventsLimitArchive   = 1000;
inline constexpr int kMaxSegmentsLimitArchive = 1000;
inline constexpr int kMaxAuditLimit           = 1000;
inline constexpr int kMaxLogLines             = 2000;

} // namespace nvr::api
