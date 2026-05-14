#include "nvr/api/RouteArchiveEvents.hpp"

#include "nvr/api/HttpCommon.hpp"
#include "nvr/api/HttpRouteHelpers.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <vector>
#include <array>
#include <algorithm>
#include <unordered_map>

#include <crow/app.h>

#include "nvr/Config.hpp"
#include "nvr/Logger.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace nvr::api {

namespace {

std::string isoUtcFromUnix(int64_t unix_sec) {
    std::time_t t = static_cast<std::time_t>(unix_sec);
    std::tm     tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

bool segmentPathUnderArchive(const fs::path& fp, const ArchiveConfig& ac) {
    if (pathInsideRoot(fp, fs::path(ac.root_path))) return true;
    for (const auto& r : ac.extra_archive_roots) {
        if (r.empty()) continue;
        if (pathInsideRoot(fp, fs::path(r))) return true;
    }
    return false;
}

/// Max bytes read into RAM for one Range response (clients re-request further ranges).
constexpr size_t kArchiveRangeBodyMax = 4 * 1024 * 1024;
constexpr size_t kArchiveRangeChunk   = 64 * 1024;

std::string readFileRangeChunked(const fs::path& fp, size_t start, size_t len) {
    std::ifstream f(fp, std::ios::binary);
    if (!f) return {};
    f.seekg(static_cast<std::streamoff>(start));
    const size_t cap = std::min(len, kArchiveRangeBodyMax);
    std::string  out;
    out.reserve(cap);
    std::array<char, kArchiveRangeChunk> chunk{};
    size_t remain = cap;
    while (remain > 0) {
        const size_t n = std::min(kArchiveRangeChunk, remain);
        f.read(chunk.data(), static_cast<std::streamsize>(n));
        const size_t got = static_cast<size_t>(f.gcount());
        if (got == 0) break;
        out.append(chunk.data(), got);
        remain -= got;
    }
    return out;
}

/// Shared body for play vs export (same file, different RBAC and Content-Disposition).
crow::response archiveSegmentMp4(
    store::ConfigStore&                                                 store,
    const crow::request&                                                req,
    int                                                                 id,
    const std::function<std::optional<Identity>(const crow::request&, Role)>& require_auth_stream,
    Role                                                                required_role,
    bool                                                                attachment,
    const char*                                                         audit_action) {
    auto who = require_auth_stream(req, required_role);
    if (!who) return required_role == Role::Viewer ? unauthorized() : forbidden();
    std::string path;
    std::string cam_id;
    {
        std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
        SQLite::Statement q(store.db().raw(), "SELECT camera_id, path FROM segments WHERE id=?");
        q.bind(1, id);
        if (!q.executeStep()) return notFound();
        if (q.getColumn(0).isNull()) return notFound();
        cam_id = q.getColumn(0).getString();
        path   = q.getColumn(1).getString();
    }
    if (!cameraVisibleToUser(store, *who, cam_id)) return forbidden();
    const ArchiveConfig ac         = store.archiveConfig();
    fs::path            primary_rt = ac.root_path;
    fs::path            fp(path);
    if (!segmentPathUnderArchive(fp, ac) || !isRegularFileNoSymlink(fp)) return notFound();
    auto ext = fp.extension().string();
    if (ext != ".mp4" && ext != ".mkv" && ext != ".m4s") return notFound();
    std::error_code ec;
    auto            size = fs::file_size(fp, ec);
    if (ec) return notFound();
    if (size == 0) return notFound();

    const auto method = req.method;
    if (method != crow::HTTPMethod::Get && method != crow::HTTPMethod::Head) {
        return jsonResp(405, {{"error", "method_not_allowed"}});
    }

    if (req.get_header_value("X-NVR-Archive-Accel") == "1" &&
        pathInsideRoot(fp, primary_rt.lexically_normal())) {
        std::error_code er;
        auto            rel =
            fs::relative(fp.lexically_normal(), primary_rt.lexically_normal(), er);
        if (!er) {
            auto rel_s = rel.generic_string();
            if (!rel_s.empty() && rel_s.find("..") == std::string::npos) {
                crow::response r(200, "");
                r.set_header("X-Accel-Redirect", "/_nvr_accel/" + rel_s);
                r.set_header("X-Accel-Buffering", "no");
                r.set_header("Content-Type", "video/mp4");
                r.set_header("Accept-Ranges", "bytes");
                if (attachment) {
                    r.set_header("Content-Disposition",
                                 "attachment; filename=\"segment-" + std::to_string(id) + ".mp4\"");
                }
                if (attachment && audit_action) {
                    store.db().audit(who->login, audit_action, std::to_string(id), "{}");
                }
                applySecurityHeaders(r);
                return r;
            }
        }
    }

    auto        range_hdr = req.get_header_value("Range");
    const bool  has_range = range_hdr.rfind("bytes=", 0) == 0;
    size_t      start     = 0;
    size_t      end       = size - 1;
    if (has_range) {
        auto eq = range_hdr.find('-', 6);
        try { start = std::stoull(range_hdr.substr(6, eq - 6)); } catch (...) {}
        if (eq != std::string::npos && eq + 1 < range_hdr.size()) {
            try { end = std::stoull(range_hdr.substr(eq + 1)); } catch (...) {}
        }
        if (end >= size) end = size - 1;
    }
    const size_t len = (end >= start) ? (end - start + 1) : 0;

    if (has_range && start >= size) {
        crow::response r = jsonResp(416, {{"error", "range_not_satisfiable"}});
        r.set_header("Content-Range", "bytes */" + std::to_string(size));
        return r;
    }

    auto finish = [&](crow::response& r) {
        r.set_header("Content-Type", "video/mp4");
        r.set_header("Accept-Ranges", "bytes");
        if (attachment) {
            r.set_header("Content-Disposition",
                         "attachment; filename=\"segment-" + std::to_string(id) + ".mp4\"");
        }
        if (attachment && audit_action) {
            store.db().audit(who->login, audit_action, std::to_string(id), "{}");
        }
        applySecurityHeaders(r);
        return r;
    };

    // HEAD without Range — metadata only
    if (method == crow::HTTPMethod::Head && !has_range) {
        crow::response r(200, "");
        r.skip_body            = true;
        r.manual_length_header = true;
        r.set_header("Content-Length", std::to_string(size));
        return finish(r);
    }

    // GET without Range — Crow streams the file from disk in small blocks (no huge `body` string).
    if (method == crow::HTTPMethod::Get && !has_range) {
        crow::response r;
        r.set_static_file_info_unsafe(fp.string());
        if (r.code == 404) return notFound();
        return finish(r);
    }

    // HEAD with Range — report slice size without reading file bytes
    if (method == crow::HTTPMethod::Head && has_range) {
        if (len == 0) return notFound();
        const size_t send_len = std::min(len, kArchiveRangeBodyMax);
        crow::response r(206, "");
        r.skip_body            = true;
        r.manual_length_header = true;
        r.set_header("Content-Length", std::to_string(send_len));
        std::ostringstream cr;
        cr << "bytes " << start << "-" << (start + send_len - 1) << "/" << size;
        r.set_header("Content-Range", cr.str());
        return finish(r);
    }

    // GET with Range (or fallback): bounded RAM, read in 64 KiB chunks
    if (len == 0) return notFound();
    std::string buf = readFileRangeChunked(fp, start, len);
    if (buf.empty()) return notFound();

    crow::response r(206, buf);
    std::ostringstream cr;
    cr << "bytes " << start << "-" << (start + buf.size() - 1) << "/" << size;
    r.set_header("Content-Range", cr.str());
    return finish(r);
}

constexpr int64_t kMaxExportBytes    = 256LL * 1024 * 1024;
constexpr int     kMaxExportSegments = 24;

std::string escapeForConcatFile(const std::string& path) {
    std::string o;
    o.reserve(path.size() + 8);
    for (char c : path) {
        if (c == '\'') o += "'\\''";
        else o += c;
    }
    return o;
}

} // namespace

void register_archive_event_routes(
    crow::SimpleApp&                                              app,
    store::ConfigStore&                                           store,
    std::function<std::optional<Identity>(const crow::request&, Role)> require_auth,
    std::function<std::optional<Identity>(const crow::request&, Role)> require_auth_stream) {

    CROW_ROUTE(app, "/api/v1/events")
    ([&store, require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        int limit = 100;
        if (auto l = req.url_params.get("limit")) try { limit = std::stoi(l); } catch (...) {}
        limit = clampLimit(limit, 1, kMaxEventsLimitArchive);
        std::string cam, type, sev, from, to, ack_q;
        bool        has_clip = false;
        bool        has_clip_filter = false;
        if (auto v = req.url_params.get("camera_id")) cam = v;
        if (auto v = req.url_params.get("type")) type = v;
        if (auto v = req.url_params.get("severity")) sev = v;
        if (auto v = req.url_params.get("from")) from = v;
        if (auto v = req.url_params.get("to")) to = v;
        if (auto v = req.url_params.get("has_clip")) {
            has_clip_filter = true;
            has_clip        = std::string(v) == "true";
        }
        if (auto v = req.url_params.get("ack")) ack_q = v;

        if (!cam.empty() && !cameraVisibleToUser(store, *who, cam)) return forbidden();

        std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
        std::string sql = "SELECT id, camera_id, ts, type, severity, payload_json, snapshot_path, "
                          "clip_path, acknowledged FROM events WHERE 1=1";
        if (!cam.empty()) sql += " AND camera_id=?";
        if (!type.empty()) sql += " AND type=?";
        if (!sev.empty()) sql += " AND severity=?";
        if (!from.empty()) sql += " AND ts >= ?";
        if (!to.empty()) sql += " AND ts <= ?";
        if (has_clip_filter) sql += has_clip ? " AND clip_path IS NOT NULL" : " AND clip_path IS NULL";
        if (!ack_q.empty()) sql += ack_q == "true" ? " AND acknowledged=1" : " AND acknowledged=0";
        sql += " ORDER BY id DESC LIMIT ?";

        SQLite::Statement q(store.db().raw(), sql);
        int               idx = 1;
        if (!cam.empty()) q.bind(idx++, cam);
        if (!type.empty()) q.bind(idx++, type);
        if (!sev.empty()) q.bind(idx++, sev);
        if (!from.empty()) q.bind(idx++, from);
        if (!to.empty()) q.bind(idx++, to);
        q.bind(idx++, limit);

        json out = json::array();
        while (q.executeStep()) {
            const std::string cid = q.getColumn(1).isNull() ? "" : q.getColumn(1).getString();
            if (!cid.empty() && !cameraVisibleToUser(store, *who, cid)) continue;
            out.push_back({
                {"id",            q.getColumn(0).getInt64()},
                {"camera_id",     cid},
                {"ts",            q.getColumn(2).getString()},
                {"type",          q.getColumn(3).getString()},
                {"severity",      q.getColumn(4).getString()},
                {"payload",       json::parse(q.getColumn(5).getString(), nullptr, false)},
                {"snapshot_path", q.getColumn(6).isNull() ? "" : q.getColumn(6).getString()},
                {"clip_path",     q.getColumn(7).isNull() ? "" : q.getColumn(7).getString()},
                {"acknowledged",  q.getColumn(8).getInt() != 0},
            });
        }
        return jsonResp(200, out);
    });

    CROW_ROUTE(app, "/api/v1/events/<int>/ack").methods(crow::HTTPMethod::POST)
    ([&store, require_auth](const crow::request& req, int id) {
        auto who = require_auth(req, Role::Operator);
        if (!who) return forbidden();
        std::string e_cam;
        {
            std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
            SQLite::Statement q(store.db().raw(), "SELECT camera_id FROM events WHERE id=?");
            q.bind(1, id);
            if (!q.executeStep()) return jsonResp(404, {{"ok", false}});
            if (!q.getColumn(0).isNull()) e_cam = q.getColumn(0).getString();
        }
        if (!e_cam.empty() && !cameraVisibleToUser(store, *who, e_cam)) return forbidden();
        bool ok = store.db().ackEvent(id, who->login);
        if (ok) store.db().audit(who->login, "events.ack", std::to_string(id), "{}");
        return jsonResp(ok ? 200 : 404, {{"ok", ok}});
    });

    CROW_ROUTE(app, "/api/v1/events/ack-all").methods(crow::HTTPMethod::POST)
    ([&store, require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Admin);
        if (!who) return forbidden();
        std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
        SQLite::Statement q(store.db().raw(),
                            "UPDATE events SET acknowledged=1 WHERE acknowledged=0");
        int n = q.exec();
        store.db().audit(who->login, "events.ack_all", "", json{{"count", n}}.dump());
        return jsonResp(200, {{"updated", n}});
    });

    CROW_ROUTE(app, "/api/v1/archive/roots-health")
    ([&store, require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Operator);
        if (!who) return unauthorized();
        const ArchiveConfig ac = store.archiveConfig();
        json                  arr = json::array();
        std::vector<std::string> roots = {ac.root_path};
        for (const auto& e : ac.extra_archive_roots) {
            if (!e.empty()) roots.push_back(e);
        }
        for (const auto& r : roots) {
            if (r.empty()) continue;
            json        row;
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
                    const auto probe = p / ".nvr_write_probe";
                    std::ofstream o(probe, std::ios::binary | std::ios::trunc);
                    if (o) {
                        o.put('1');
                        o.flush();
                        writable = o.good();
                    }
                    o.close();
                    std::error_code ec2;
                    fs::remove(probe, ec2);
                } catch (...) {
                    writable = false;
                }
            }
            row["writable"] = writable;
            arr.push_back(std::move(row));
        }
        return jsonResp(200, {{"roots", arr}});
    });

    CROW_ROUTE(app, "/api/v1/archive/timeline")
    ([&store, require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        std::string cam = req.url_params.get("camera_id") ? std::string(req.url_params.get("camera_id")) : "";
        std::string from, to;
        if (auto v = req.url_params.get("from")) from = v;
        if (auto v = req.url_params.get("to")) to = v;
        int bucket_minutes = 5;
        if (auto v = req.url_params.get("bucket_minutes")) try {
            bucket_minutes = std::stoi(v);
        } catch (...) {}
        bucket_minutes = clampLimit(bucket_minutes, 1, 24 * 60);
        if (cam.empty()) return jsonResp(400, {{"error", "camera_id_required"}});
        if (from.empty() || to.empty())
            return jsonResp(400, {{"error", "from_and_to_required"}});
        if (!cameraVisibleToUser(store, *who, cam)) return forbidden();

        const int bucket_sec = bucket_minutes * 60;

        std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
        SQLite::Statement q(store.db().raw(),
                            "SELECT (CAST(strftime('%s', started_at) AS INTEGER) / ?) * ? AS b0, "
                            "MAX(has_motion) AS hm, COUNT(*) AS cnt, SUM(duration_ms) AS dur "
                            "FROM segments WHERE camera_id = ? AND ended_at > ? AND started_at < ? "
                            "GROUP BY b0 ORDER BY b0");
        int bi = 1;
        q.bind(bi++, bucket_sec);
        q.bind(bi++, bucket_sec);
        q.bind(bi++, cam);
        q.bind(bi++, from);
        q.bind(bi++, to);

        json buckets = json::array();
        while (q.executeStep()) {
            int64_t b0  = q.getColumn(0).getInt64();
            bool    hm  = q.getColumn(1).getInt() != 0;
            int64_t cnt = q.getColumn(2).getInt64();
            int64_t dur = q.getColumn(3).isNull() ? 0 : q.getColumn(3).getInt64();
            buckets.push_back({{"bucket_start", isoUtcFromUnix(b0)},
                               {"bucket_start_unix", b0},
                               {"segment_count", cnt},
                               {"has_motion", hm},
                               {"duration_ms_total", dur},
                               {"event_count", 0}});
        }

        std::unordered_map<int64_t, int64_t> ev_by_bucket;
        {
            SQLite::Statement qev(
                store.db().raw(),
                "SELECT (CAST(strftime('%s', ts) AS INTEGER) / ?) * ? AS b0, COUNT(*) AS n "
                "FROM events WHERE camera_id = ? AND ts > ? AND ts < ? GROUP BY b0");
            int ei = 1;
            qev.bind(ei++, bucket_sec);
            qev.bind(ei++, bucket_sec);
            qev.bind(ei++, cam);
            qev.bind(ei++, from);
            qev.bind(ei++, to);
            while (qev.executeStep())
                ev_by_bucket[qev.getColumn(0).getInt64()] = qev.getColumn(1).getInt64();
        }
        for (auto& b : buckets) {
            const int64_t u = b["bucket_start_unix"].get<int64_t>();
            auto            it = ev_by_bucket.find(u);
            b["event_count"]     = it != ev_by_bucket.end() ? it->second : 0;
        }

        return jsonResp(200,
                        {{"bucket_seconds", bucket_sec},
                         {"bucket_minutes", bucket_minutes},
                         {"camera_id", cam},
                         {"from", from},
                         {"to", to},
                         {"buckets", buckets}});
    });

    CROW_ROUTE(app, "/api/v1/archive/segments")
    ([&store, require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Viewer);
        if (!who) return unauthorized();
        std::string cam, from, to;
        int         limit = 500;
        if (auto v = req.url_params.get("camera_id")) cam = v;
        if (auto v = req.url_params.get("from")) from = v;
        if (auto v = req.url_params.get("to")) to = v;
        if (auto v = req.url_params.get("limit")) try { limit = std::stoi(v); } catch (...) {}
        limit = clampLimit(limit, 1, kMaxSegmentsLimitArchive);
        if (!cam.empty() && !cameraVisibleToUser(store, *who, cam)) return forbidden();
        std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
        std::string sql = "SELECT id, camera_id, path, started_at, ended_at, duration_ms, "
                          "size_bytes, has_motion FROM segments WHERE 1=1";
        if (!cam.empty()) sql += " AND camera_id=?";
        if (!from.empty()) sql += " AND started_at >= ?";
        if (!to.empty()) sql += " AND started_at <= ?";
        sql += " ORDER BY started_at DESC LIMIT ?";
        SQLite::Statement q(store.db().raw(), sql);
        int               idx = 1;
        if (!cam.empty()) q.bind(idx++, cam);
        if (!from.empty()) q.bind(idx++, from);
        if (!to.empty()) q.bind(idx++, to);
        q.bind(idx++, limit);

        json out = json::array();
        while (q.executeStep()) {
            const std::string sc = q.getColumn(1).getString();
            if (!cameraVisibleToUser(store, *who, sc)) continue;
            out.push_back({
                {"id",          q.getColumn(0).getInt64()},
                {"camera_id",   sc},
                {"path",        q.getColumn(2).getString()},
                {"started_at",  q.getColumn(3).getString()},
                {"ended_at",    q.getColumn(4).getString()},
                {"duration_ms", q.getColumn(5).getInt64()},
                {"size_bytes",  q.getColumn(6).getInt64()},
                {"has_motion",  q.getColumn(7).getInt() != 0},
            });
        }
        return jsonResp(200, out);
    });

    CROW_ROUTE(app, "/api/v1/archive/segments/<int>/play.mp4")
    ([&store, require_auth_stream](const crow::request& req, int id) {
        return archiveSegmentMp4(store, req, id, require_auth_stream, Role::Viewer, false, nullptr);
    });

    CROW_ROUTE(app, "/api/v1/archive/segments/<int>/export.mp4")
    ([&store, require_auth_stream](const crow::request& req, int id) {
        return archiveSegmentMp4(store, req, id, require_auth_stream, Role::Operator, true,
                                 "archive.segment_export");
    });

    CROW_ROUTE(app, "/api/v1/archive/export").methods(crow::HTTPMethod::POST)
    ([&store, require_auth](const crow::request& req) {
        auto who = require_auth(req, Role::Operator);
        if (!who) return forbidden();
        json body;
        try {
            body = json::parse(req.body, nullptr, false);
        } catch (...) {
            return jsonResp(400, {{"error", "bad_json"}});
        }
        if (body.is_discarded() || !body.is_object())
            return jsonResp(400, {{"error", "bad_json"}});
        std::string cam  = body.value("camera_id", "");
        std::string from = body.value("from", "");
        std::string to   = body.value("to", "");
        if (cam.empty() || from.empty() || to.empty())
            return jsonResp(400, {{"error", "camera_id_from_to_required"}});
        if (!isSafeToken(cam, 96)) return jsonResp(400, {{"error", "bad_camera_id"}});
        if (!cameraVisibleToUser(store, *who, cam)) return forbidden();
        if (from.size() > 64 || to.size() > 64) return jsonResp(400, {{"error", "bad_range"}});
        if (from >= to) return jsonResp(400, {{"error", "from_must_be_before_to"}});
        if (body.contains("watermark_text") && !body["watermark_text"].is_string())
            return jsonResp(400, {{"error", "watermark_text_must_be_string"}});

        struct TmpExport {
            fs::path root;
            ~TmpExport() {
                if (!root.empty()) {
                    std::error_code ec;
                    fs::remove_all(root, ec);
                }
            }
        };
        TmpExport tmp;

        std::vector<std::string> paths;
        int64_t                  sum_bytes = 0;
        {
            std::lock_guard<std::recursive_mutex> lk(store.db().mutex());
            SQLite::Statement q(
                store.db().raw(),
                "SELECT path, size_bytes FROM segments WHERE camera_id=? AND ended_at > ? AND "
                "started_at < ? ORDER BY started_at ASC LIMIT ?");
            q.bind(1, cam);
            q.bind(2, from);
            q.bind(3, to);
            q.bind(4, kMaxExportSegments);
            while (q.executeStep()) {
                std::string p   = q.getColumn(0).getString();
                int64_t     szb = q.getColumn(1).getInt64();
                if (szb > 0) sum_bytes += szb;
                paths.push_back(std::move(p));
            }
        }
        if (paths.empty()) return jsonResp(404, {{"error", "no_segments"}});
        if (sum_bytes > kMaxExportBytes) {
            return jsonResp(413,
                            {{"error", "export_too_large"}, {"max_bytes", kMaxExportBytes}});
        }

        const ArchiveConfig arch_cfg = store.archiveConfig();
        for (const auto& pstr : paths) {
            fs::path fp(pstr);
            if (!segmentPathUnderArchive(fp, arch_cfg) || !isRegularFileNoSymlink(fp))
                return jsonResp(400, {{"error", "bad_segment_path"}});
            auto ext = fp.extension().string();
            if (ext != ".mp4" && ext != ".mkv" && ext != ".m4s")
                return jsonResp(400, {{"error", "unsupported_segment_container"}});
        }

        std::random_device rd;
        tmp.root =
            fs::temp_directory_path() /
            ("nvr-export-" + std::to_string(rd()) + "-" +
             std::to_string(
                 std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::create_directories(tmp.root, ec);
        if (ec) return jsonResp(500, {{"error", "temp_dir_failed"}});

        fs::path list_f = tmp.root / "list.txt";
        fs::path out_f  = tmp.root / "clip.mp4";
        {
            std::ofstream list(list_f, std::ios::binary | std::ios::trunc);
            if (!list) return jsonResp(500, {{"error", "list_open_failed"}});
            for (const auto& pstr : paths) {
                std::error_code ec2;
                fs::path        ap = fs::weakly_canonical(fs::path(pstr), ec2);
                if (ec2) return jsonResp(500, {{"error", "bad_path"}});
                list << "file '" << escapeForConcatFile(ap.string()) << "'\n";
            }
        }

        std::vector<std::string> argv = {"ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f",
                                          "concat", "-safe",   "0", "-i",       list_f.string(),
                                          "-c",     "copy",     out_f.string()};
        std::string              ff_log = runArgv(argv, 1 << 20);
        (void)ff_log;
        if (!fs::is_regular_file(out_f)) {
            NVR_WARN("archive", "ffmpeg concat failed for camera %s", cam.c_str());
            return jsonResp(502, {{"error", "ffmpeg_failed"}});
        }

        fs::path    final_out = out_f;
        std::string wm_raw;
        if (body.contains("watermark_text") && body["watermark_text"].is_string())
            wm_raw = body["watermark_text"].get<std::string>();
        else
            wm_raw = arch_cfg.export_watermark_text;
        if (!wm_raw.empty()) {
            if (wm_raw.size() > 512) wm_raw.resize(512);
            for (auto& ch : wm_raw) {
                if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
            }
            fs::path wm_txt = tmp.root / "wm.txt";
            {
                std::ofstream wf(wm_txt, std::ios::binary | std::ios::trunc);
                if (!wf) return jsonResp(500, {{"error", "wm_file_failed"}});
                wf << wm_raw;
            }
            fs::path wm_out = tmp.root / "clip_wm.mp4";
            std::string vf =
                "drawtext=textfile=" + wm_txt.string() +
                ":reload=0:fontsize=18:fontcolor=white@0.85:x=w-text_w-12:y=h-text_h-8:box=1:"
                "boxcolor=black@0.5";
            std::vector<std::string> wm_argv = {
                "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", out_f.string(),
                "-vf", vf, "-c:v", "libx264", "-preset", "veryfast", "-crf", "23", "-movflags",
                "+faststart", "-c:a", "copy", wm_out.string()};
            runArgv(wm_argv, 1 << 20);
            if (fs::is_regular_file(wm_out)) {
                auto wsz = fs::file_size(wm_out, ec);
                if (!ec && wsz > 0 && static_cast<int64_t>(wsz) <= kMaxExportBytes)
                    final_out = wm_out;
                else
                    NVR_WARN("archive", "watermark output rejected; using concat copy");
            } else {
                NVR_WARN("archive", "watermark ffmpeg failed; using concat copy");
            }
        }

        auto out_sz = fs::file_size(final_out, ec);
        if (ec || out_sz == 0 || static_cast<int64_t>(out_sz) > kMaxExportBytes)
            return jsonResp(502, {{"error", "export_output_invalid"}});

        std::string buf;
        if (!readFile(final_out, buf)) return jsonResp(502, {{"error", "read_failed"}});

        json audit_payload = {{"from", from}, {"to", to}, {"segments", paths.size()}};
        if (body.contains("watermark_text")) audit_payload["watermark_text_override"] = true;
        store.db().audit(who->login, "archive.export_range", cam, audit_payload.dump());

        crow::response r(200, buf);
        r.set_header("Content-Type", "video/mp4");
        std::string fn = "export-" + cam + ".mp4";
        for (auto& c : fn) {
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.' || c == '_'))
                c = '_';
        }
        r.set_header("Content-Disposition", "attachment; filename=\"" + fn + "\"");
        applySecurityHeaders(r);
        return r;
    });
}

} // namespace nvr::api
