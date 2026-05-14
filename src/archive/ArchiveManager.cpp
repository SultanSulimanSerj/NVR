#include "nvr/ArchiveManager.hpp"

#include "nvr/Logger.hpp"
#include "nvr/obs/Metrics.hpp"

#include <sys/statvfs.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace nvr {

namespace {

std::string formatTimestamp(std::chrono::system_clock::time_point tp) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

struct DiskUsage {
    uint64_t total_bytes{0};
    uint64_t free_bytes{0};
    bool     valid{false};
};

DiskUsage queryDiskUsage(const fs::path& p) {
    DiskUsage u;
    struct statvfs s {};
    if (::statvfs(p.c_str(), &s) != 0) return u;

    u.total_bytes = static_cast<uint64_t>(s.f_blocks) * s.f_frsize;
    u.free_bytes  = static_cast<uint64_t>(s.f_bavail) * s.f_frsize;
    u.valid       = true;
    return u;
}

std::chrono::system_clock::time_point toSystemTime(fs::file_time_type ftime) {
    using namespace std::chrono;
    const auto sys_now  = system_clock::now();
    const auto file_now = fs::file_time_type::clock::now();
    return time_point_cast<system_clock::duration>(ftime - file_now + sys_now);
}

} // namespace

void ArchiveManager::rebuildRoots_() {
    roots_.clear();
    roots_.push_back(fs::path(cfg_.root_path));
    for (const auto& s : cfg_.extra_archive_roots) {
        if (s.empty()) continue;
        fs::path p(s);
        if (std::find(roots_.begin(), roots_.end(), p) != roots_.end()) continue;
        roots_.push_back(std::move(p));
    }
}

bool ArchiveManager::pathUnderAnyRoot_(const fs::path& p) const {
    std::error_code ec;
    auto            pc = fs::weakly_canonical(p, ec);
    if (ec) return false;
    for (const auto& r : roots_) {
        auto rc = fs::weakly_canonical(r, ec);
        if (ec) continue;
        auto ps = pc.lexically_normal().string();
        auto rs = rc.lexically_normal().string();
        if (!rs.empty() && rs.back() != '/') rs.push_back('/');
        if (ps.rfind(rs, 0) == 0 || ps == rc.lexically_normal().string()) return true;
    }
    return false;
}

fs::path ArchiveManager::pickWriteRoot_() const {
    fs::path  best = roots_.empty() ? fs::path(cfg_.root_path) : roots_.front();
    uint64_t best_free = 0;
    for (const auto& r : roots_) {
        auto u = queryDiskUsage(r);
        if (!u.valid) continue;
        if (u.free_bytes > best_free) {
            best_free = u.free_bytes;
            best      = r;
        }
    }
    return best;
}

ArchiveManager::ArchiveManager(ArchiveConfig cfg) : cfg_(std::move(cfg)) {
    rebuildRoots_();
    std::error_code ec;
    for (const auto& r : roots_) {
        fs::create_directories(r, ec);
        if (ec) {
            NVR_WARN("archive", "could not create archive root '%s': %s", r.c_str(), ec.message().c_str());
        }
    }

    if (cfg_.release_to_ratio >= cfg_.target_usage_ratio) {
        cfg_.release_to_ratio = std::max(0.0, cfg_.target_usage_ratio - 0.02);
    }
}

ArchiveManager::~ArchiveManager() { stop(); }

void ArchiveManager::start() {
    if (running_.exchange(true)) return;
    worker_ = std::thread(&ArchiveManager::retentionLoop, this);
    std::ostringstream roots_log;
    for (size_t i = 0; i < roots_.size(); ++i) {
        if (i) roots_log << ", ";
        roots_log << roots_[i].string();
    }
    NVR_INFO("archive",
             "started: roots=[%s], segment=%dmin, target=%.2f, release_to=%.2f, min_keep=%dmin",
             roots_log.str().c_str(), cfg_.segment_minutes, cfg_.target_usage_ratio,
             cfg_.release_to_ratio, cfg_.min_keep_minutes);
}

void ArchiveManager::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
    NVR_INFO("archive", "stopped");
}

fs::path ArchiveManager::nextSegmentPath(const std::string& camera_id,
                                         std::chrono::system_clock::time_point ts) const {
    fs::path        base   = pickWriteRoot_();
    fs::path        cam_dir = base / camera_id;
    std::error_code ec;
    fs::create_directories(cam_dir, ec);

    std::string name =
        cfg_.file_prefix + camera_id + "_" + formatTimestamp(ts) + cfg_.file_extension;
    return cam_dir / name;
}

void ArchiveManager::registerSegment(const fs::path& path) {
    SegmentInfo si;
    si.path       = path;
    si.ended_at   = std::chrono::system_clock::now();
    si.started_at = si.ended_at;
    std::error_code ec;
    si.size_bytes = static_cast<int64_t>(fs::file_size(path, ec));
    registerSegment(si);
}

void ArchiveManager::registerSegment(const SegmentInfo& info) {
    NVR_INFO("archive", "segment closed: %s (%.2f MiB)",
             info.path.c_str(),
             static_cast<double>(info.size_bytes) / (1024.0 * 1024.0));
    if (on_finalized_) {
        try { on_finalized_(info); }
        catch (const std::exception& e) {
            NVR_WARN("archive", "finalize callback threw: %s", e.what());
        }
    }
    enforceQuotaNow();
}

void ArchiveManager::setOnSegmentFinalized(FinalizeCb cb) { on_finalized_ = std::move(cb); }
void ArchiveManager::setOnSegmentEvicted(EvictCb cb) { on_evicted_ = std::move(cb); }

bool ArchiveManager::shouldRotateSegment(
    std::chrono::system_clock::time_point segment_started_at) const noexcept {
    auto elapsed = std::chrono::system_clock::now() - segment_started_at;
    return elapsed >= segmentDuration();
}

double ArchiveManager::currentUsageRatio() const {
    double worst = 0.0;
    for (const auto& r : roots_) {
        auto u = queryDiskUsage(r);
        if (!u.valid || u.total_bytes == 0) continue;
        auto used = u.total_bytes - u.free_bytes;
        worst       = std::max(worst, static_cast<double>(used) / static_cast<double>(u.total_bytes));
    }
    return worst;
}

bool ArchiveManager::isOwnedFile(const fs::path& p) const {
    if (!p.has_filename()) return false;
    if (!pathUnderAnyRoot_(p)) return false;
    auto ext = p.extension().string();
    if (ext == ".tmp") return false;
    if (ext != cfg_.file_extension) return false;
    auto name = p.filename().string();
    return name.rfind(cfg_.file_prefix, 0) == 0;
}

void ArchiveManager::enforceQuotaNow() {
    std::lock_guard<std::mutex> lk(io_mu_);

    for (const fs::path& scan_root : roots_) {
        DiskUsage u = queryDiskUsage(scan_root);
        if (!u.valid) {
            NVR_WARN("archive", "statvfs failed for '%s'", scan_root.c_str());
            continue;
        }

        auto usageRatio = [&]() {
            if (u.total_bytes == 0) return 0.0;
            return static_cast<double>(u.total_bytes - u.free_bytes) /
                   static_cast<double>(u.total_bytes);
        };

        if (usageRatio() < cfg_.target_usage_ratio) continue;

        NVR_WARN("archive", "disk '%s' usage %.2f%% >= target %.2f%%, reclaiming", scan_root.c_str(),
                 usageRatio() * 100.0, cfg_.target_usage_ratio * 100.0);

        struct Entry {
            fs::path path;
            std::chrono::system_clock::time_point mtime;
            uint64_t size;
        };

        std::vector<Entry> entries;
        entries.reserve(1024);

        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(scan_root, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) {
                NVR_WARN("archive", "scan error: %s", ec.message().c_str());
                ec.clear();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            if (!isOwnedFile(it->path())) continue;

            auto ftime = fs::last_write_time(it->path(), ec);
            if (ec) {
                ec.clear();
                continue;
            }
            auto sctp = toSystemTime(ftime);

            auto sz = fs::file_size(it->path(), ec);
            if (ec) {
                ec.clear();
                continue;
            }
            entries.push_back({it->path(), sctp, sz});
        }

        std::sort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b) { return a.mtime < b.mtime; });

        const auto now      = std::chrono::system_clock::now();
        const auto min_keep = std::chrono::minutes(cfg_.min_keep_minutes);
        size_t     removed_count = 0;
        uint64_t  freed_bytes    = 0;

        for (const auto& e : entries) {
            if (usageRatio() <= cfg_.release_to_ratio) break;
            if ((now - e.mtime) < min_keep) {
                NVR_DEBUG("archive", "skip too-young file: %s", e.path.c_str());
                continue;
            }

            std::error_code rmec;
            if (fs::remove(e.path, rmec)) {
                u.free_bytes += e.size;
                ++removed_count;
                freed_bytes += e.size;
                obs::Registry::instance().global().archive_evictions_total.inc();
                if (on_evicted_) {
                    try { on_evicted_(e.path); }
                    catch (const std::exception& ex) {
                        NVR_WARN("archive", "evict callback threw: %s", ex.what());
                    }
                }
                NVR_INFO("archive", "removed old segment: %s (%.2f MiB)",
                         e.path.c_str(),
                         static_cast<double>(e.size) / (1024.0 * 1024.0));
            } else {
                NVR_WARN("archive", "failed to remove %s: %s",
                         e.path.c_str(), rmec.message().c_str());
            }
        }

        NVR_INFO("archive",
                 "reclaim on '%s': removed=%zu files, freed=%.2f MiB, usage now %.2f%%",
                 scan_root.c_str(), removed_count,
                 static_cast<double>(freed_bytes) / (1024.0 * 1024.0), usageRatio() * 100.0);
    }

    obs::Registry::instance().global().archive_used_ratio.set(currentUsageRatio());
}

void ArchiveManager::retentionLoop() {
    while (running_.load()) {
        try {
            enforceQuotaNow();
        } catch (const std::exception& e) {
            NVR_ERROR("archive", "retention exception: %s", e.what());
        }
        for (int i = 0; i < 60 && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

} // namespace nvr
