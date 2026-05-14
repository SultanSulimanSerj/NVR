#pragma once

#include "nvr/Config.hpp"
#include "nvr/FFmpegDecoder.hpp"

#include <filesystem>
#include <memory>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace nvr {

struct AVFormatOutputDeleter { void operator()(AVFormatContext* p) const noexcept; };
using OutputPtr = std::unique_ptr<AVFormatContext, AVFormatOutputDeleter>;

class SegmentWriter {
public:
    SegmentWriter();
    ~SegmentWriter();

    bool open(const std::filesystem::path& path,
              AVCodecContext* decoder_ctx,
              AVStream*       input_video_stream,
              HwAccel         hw);

    bool writePacket(AVPacket* pkt, AVRational src_time_base);
    void close();

    bool isOpen() const noexcept { return out_.get() != nullptr; }
    bool seenKeyFrame() const noexcept { return first_keyframe_; }
    // After close(): the *final* path on disk (rename of the .tmp).
    // While the writer is open, this is still the requested path; the on-disk file is path() + ".tmp".
    const std::filesystem::path& path() const noexcept { return path_; }
    uint64_t bytesWritten() const noexcept { return bytes_written_; }

    // True only if the trailer was written, file fsync'd and renamed atomically
    // from .tmp to its final name. Retention logic / DB inserts must check this.
    bool wasFinalized() const noexcept { return finalized_ok_; }

private:
    OutputPtr             out_{};
    AVStream*             out_stream_{nullptr};
    AVRational            in_time_base_{0, 1};
    int64_t               first_dts_{AV_NOPTS_VALUE};
    int64_t               first_pts_{AV_NOPTS_VALUE};
    int64_t               last_dts_{AV_NOPTS_VALUE};
    int64_t               last_pts_{AV_NOPTS_VALUE};
    bool                  trailer_written_{false};
    bool                  first_keyframe_{false};
    bool                  finalized_ok_{false};
    uint64_t              bytes_written_{0};
    std::filesystem::path path_{};
    std::filesystem::path tmp_path_{};
};

}
