#include "nvr/HlsMuxer.hpp"

#include "nvr/Logger.hpp"

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/error.h>
}

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace nvr {

namespace {
std::string ffErr(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, buf, sizeof(buf));
    return buf;
}
}

HlsMuxer::HlsMuxer()  = default;
HlsMuxer::~HlsMuxer() { close(); }

bool HlsMuxer::open(const fs::path& dir, AVCodecContext* enc_ctx,
                    int segment_seconds, int list_size) {
    close();
    std::error_code ec;
    fs::create_directories(dir, ec);

    dir_     = dir;
    auto pl  = dir / "playlist.m3u8";

    if (avformat_alloc_output_context2(&out_, nullptr, "hls", pl.c_str()) < 0 || !out_) {
        NVR_ERROR("hls", "alloc output context failed for %s", pl.c_str());
        return false;
    }

    out_stream_ = avformat_new_stream(out_, nullptr);
    if (!out_stream_) {
        avformat_free_context(out_); out_ = nullptr;
        return false;
    }

    if (avcodec_parameters_from_context(out_stream_->codecpar, enc_ctx) < 0) {
        NVR_ERROR("hls", "avcodec_parameters_from_context failed");
        avformat_free_context(out_); out_ = nullptr;
        return false;
    }
    out_stream_->time_base       = enc_ctx->time_base;
    out_stream_->codecpar->codec_tag = 0;
    in_tb_                        = enc_ctx->time_base;

    AVDictionary* opts = nullptr;
    av_dict_set_int(&opts, "hls_time", segment_seconds, 0);
    av_dict_set_int(&opts, "hls_list_size", list_size, 0);
    av_dict_set    (&opts, "hls_flags", "independent_segments+delete_segments+omit_endlist", 0);
    av_dict_set    (&opts, "hls_segment_type", "fmp4", 0);
    av_dict_set    (&opts, "hls_segment_filename",
                    (dir / "seg-%05d.m4s").c_str(), 0);

    int err = avformat_write_header(out_, &opts);
    av_dict_free(&opts);
    if (err < 0) {
        NVR_ERROR("hls", "avformat_write_header: %s", ffErr(err).c_str());
        avformat_free_context(out_); out_ = nullptr;
        return false;
    }
    NVR_INFO("hls", "HLS muxer started: %s", pl.c_str());
    return true;
}

bool HlsMuxer::writePacket(AVPacket* pkt, AVRational src_tb) {
    if (!out_ || !out_stream_) return false;

    AVPacket* p = av_packet_alloc();
    if (av_packet_ref(p, pkt) < 0) { av_packet_free(&p); return false; }

    p->pts = av_rescale_q(p->pts, src_tb, out_stream_->time_base);
    p->dts = av_rescale_q(p->dts, src_tb, out_stream_->time_base);
    if (p->duration > 0)
        p->duration = av_rescale_q(p->duration, src_tb, out_stream_->time_base);
    p->pos          = -1;
    p->stream_index = out_stream_->index;

    int err = av_interleaved_write_frame(out_, p);
    av_packet_free(&p);
    if (err < 0) {
        NVR_WARN("hls", "write_frame: %s", ffErr(err).c_str());
        return false;
    }
    return true;
}

void HlsMuxer::close() {
    if (!out_) return;
    int err = av_write_trailer(out_);
    if (err < 0) NVR_WARN("hls", "trailer: %s", ffErr(err).c_str());
    if (!(out_->oformat->flags & AVFMT_NOFILE) && out_->pb) {
        avio_closep(&out_->pb);
    }
    avformat_free_context(out_);
    out_        = nullptr;
    out_stream_ = nullptr;
}

}
