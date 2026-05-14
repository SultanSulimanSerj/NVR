#pragma once

#include "nvr/Config.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nvr {

struct SegmentInfo {
    std::string camera_id;
    std::filesystem::path path;
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point ended_at;
    int64_t size_bytes{0};
    bool    has_motion{false};
};

class ArchiveManager {
public:
    explicit ArchiveManager(ArchiveConfig cfg);
    ~ArchiveManager();

    ArchiveManager(const ArchiveManager&)            = delete;
    ArchiveManager& operator=(const ArchiveManager&) = delete;

    void start();
    void stop();

    std::filesystem::path nextSegmentPath(const std::string& camera_id,
                                          std::chrono::system_clock::time_point ts) const;

    void registerSegment(const std::filesystem::path& path);
    void registerSegment(const SegmentInfo& info);

    using FinalizeCb = std::function<void(const SegmentInfo&)>;
    using EvictCb    = std::function<void(const std::filesystem::path&)>;

    void setOnSegmentFinalized(FinalizeCb cb);
    void setOnSegmentEvicted (EvictCb    cb);

    bool   shouldRotateSegment(std::chrono::system_clock::time_point segment_started_at) const noexcept;
    auto   segmentDuration() const noexcept { return std::chrono::minutes(cfg_.segment_minutes); }

    double currentUsageRatio() const;
    void   enforceQuotaNow();

private:
    bool isOwnedFile(const std::filesystem::path& p) const;
    void retentionLoop();
    void rebuildRoots_();
    bool pathUnderAnyRoot_(const std::filesystem::path& p) const;
    std::filesystem::path pickWriteRoot_() const;

    ArchiveConfig                       cfg_;
    std::vector<std::filesystem::path>  roots_;
    std::thread                         worker_;
    std::atomic<bool>                   running_{false};
    mutable std::mutex                  io_mu_;
    FinalizeCb                          on_finalized_;
    EvictCb                             on_evicted_;
};

}
