#include "nvr/FFmpegEncoder.hpp"

#include "nvr/Logger.hpp"

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
}

#include <fcntl.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace nvr {

void AVFormatOutputDeleter::operator()(AVFormatContext* p) const noexcept {
    if (!p) return;
    if (!(p->oformat->flags & AVFMT_NOFILE) && p->pb) {
        avio_closep(&p->pb);
    }
    avformat_free_context(p);
}

namespace {

std::string ffErr(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, buf, sizeof(buf));
    return buf;
}

}

SegmentWriter::SegmentWriter()  = default;
SegmentWriter::~SegmentWriter() { close(); }

bool SegmentWriter::open(const fs::path& path,
                         AVCodecContext* decoder_ctx,
                         AVStream*       input_video_stream,
                         HwAccel         /*hw*/) {
    close();
    path_     = path;
    tmp_path_ = path.string() + ".tmp";

    // Use the *real* extension when probing the output muxer (FFmpeg keys on it),
    // even though we initially write under `.tmp`.
    AVFormatContext* out_raw = nullptr;
    int err = avformat_alloc_output_context2(&out_raw, nullptr, nullptr, path.c_str());
    if (err < 0 || !out_raw) {
        NVR_ERROR("encoder", "avformat_alloc_output_context2 (%s): %s",
                  path.c_str(), ffErr(err).c_str());
        return false;
    }
    out_.reset(out_raw);

    out_stream_ = avformat_new_stream(out_.get(), nullptr);
    if (!out_stream_) {
        NVR_ERROR("encoder", "avformat_new_stream failed");
        return false;
    }

    if (avcodec_parameters_copy(out_stream_->codecpar, input_video_stream->codecpar) < 0) {
        NVR_ERROR("encoder", "avcodec_parameters_copy failed");
        return false;
    }
    out_stream_->codecpar->codec_tag = 0;
    out_stream_->time_base           = input_video_stream->time_base;
    in_time_base_                    = input_video_stream->time_base;

    if (!(out_->oformat->flags & AVFMT_NOFILE)) {
        err = avio_open(&out_->pb, tmp_path_.c_str(), AVIO_FLAG_WRITE);
        if (err < 0) {
            NVR_ERROR("encoder", "avio_open '%s': %s", tmp_path_.c_str(), ffErr(err).c_str());
            return false;
        }
    }

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "movflags", "+faststart+frag_keyframe+empty_moov", 0);

    err = avformat_write_header(out_.get(), &opts);
    av_dict_free(&opts);
    if (err < 0) {
        NVR_ERROR("encoder", "avformat_write_header: %s", ffErr(err).c_str());
        return false;
    }

    first_dts_       = AV_NOPTS_VALUE;
    first_pts_       = AV_NOPTS_VALUE;
    last_dts_        = AV_NOPTS_VALUE;
    last_pts_        = AV_NOPTS_VALUE;
    trailer_written_ = false;
    first_keyframe_  = false;
    finalized_ok_    = false;
    bytes_written_   = 0;

    NVR_INFO("encoder", "segment opened: %s (writing to %s)", path.c_str(), tmp_path_.c_str());
    (void)decoder_ctx;
    return true;
}

bool SegmentWriter::writePacket(AVPacket* pkt, AVRational src_time_base) {
    if (!out_ || !out_stream_ || !pkt) return false;

    const bool is_key = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
    if (!first_keyframe_) {
        if (!is_key) return true;  // skip B/P prefix until a true keyframe arrives
        first_keyframe_ = true;
    }

    if (first_dts_ == AV_NOPTS_VALUE) {
        first_dts_ = (pkt->dts != AV_NOPTS_VALUE) ? pkt->dts : 0;
        first_pts_ = (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts : first_dts_;
    }

    AVPacket* out_pkt = av_packet_alloc();
    if (!out_pkt) return false;
    if (av_packet_ref(out_pkt, pkt) < 0) {
        av_packet_free(&out_pkt);
        return false;
    }

    // Normalize to segment start.
    if (out_pkt->dts != AV_NOPTS_VALUE) out_pkt->dts -= first_dts_;
    if (out_pkt->pts != AV_NOPTS_VALUE) out_pkt->pts -= first_pts_;
    if (out_pkt->dts < 0) out_pkt->dts = 0;
    if (out_pkt->pts < 0) out_pkt->pts = 0;

    out_pkt->pts = av_rescale_q(out_pkt->pts, src_time_base, out_stream_->time_base);
    out_pkt->dts = av_rescale_q(out_pkt->dts, src_time_base, out_stream_->time_base);

    // Enforce strict monotonicity on DTS — some RTSP sources hand us mild wraparounds
    // that crash muxers (AVERROR(EINVAL): pts is non-monotonic). Always bump by 1.
    if (last_dts_ != AV_NOPTS_VALUE && out_pkt->dts <= last_dts_) {
        out_pkt->dts = last_dts_ + 1;
        if (out_pkt->pts < out_pkt->dts) out_pkt->pts = out_pkt->dts;
    }
    last_dts_ = out_pkt->dts;
    last_pts_ = out_pkt->pts;

    if (out_pkt->duration > 0) {
        out_pkt->duration =
            av_rescale_q(out_pkt->duration, src_time_base, out_stream_->time_base);
    }
    out_pkt->pos          = -1;
    out_pkt->stream_index = out_stream_->index;

    bytes_written_ += static_cast<uint64_t>(out_pkt->size);

    int err = av_interleaved_write_frame(out_.get(), out_pkt);
    av_packet_free(&out_pkt);

    if (err < 0) {
        NVR_WARN("encoder", "av_interleaved_write_frame: %s", ffErr(err).c_str());
        return false;
    }
    return true;
}

void SegmentWriter::close() {
    if (!out_) return;
    bool trailer_ok = true;
    if (!trailer_written_) {
        int err = av_write_trailer(out_.get());
        if (err < 0) {
            NVR_WARN("encoder", "av_write_trailer: %s", ffErr(err).c_str());
            trailer_ok = false;
        }
        trailer_written_ = true;
    }
    out_.reset();  // closes avio + frees ctx
    out_stream_ = nullptr;

    // fsync the .tmp + atomic rename to final path. Anyone observing the archive
    // therefore only ever sees fully-flushed files at the final name.
    if (trailer_ok && !tmp_path_.empty()) {
        int fd = ::open(tmp_path_.c_str(), O_RDONLY);
        if (fd >= 0) {
            ::fsync(fd);
            ::close(fd);
        }
        std::error_code ec;
        fs::rename(tmp_path_, path_, ec);
        if (ec) {
            NVR_WARN("encoder", "rename %s -> %s failed: %s",
                      tmp_path_.c_str(), path_.c_str(), ec.message().c_str());
        } else {
            finalized_ok_ = true;
            // fsync directory so the rename is durable.
            int dfd = ::open(path_.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
            if (dfd >= 0) {
                ::fsync(dfd);
                ::close(dfd);
            }
        }
    } else if (!tmp_path_.empty()) {
        // Trailer write failed — drop the .tmp so it cannot be picked up by a
        // resync that mistakes it for a recoverable fragment.
        std::error_code ec;
        fs::remove(tmp_path_, ec);
    }

    tmp_path_.clear();
    NVR_INFO("encoder", "segment closed: %s (finalized=%d)", path_.c_str(), finalized_ok_ ? 1 : 0);
}

}
