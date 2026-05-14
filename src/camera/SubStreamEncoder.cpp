#include "nvr/SubStreamEncoder.hpp"

#include "nvr/HlsMuxer.hpp"
#include "nvr/Logger.hpp"

extern "C" {
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

namespace nvr {

namespace {

std::string ffErr(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, buf, sizeof(buf));
    return buf;
}

const char* pickEncoderName(HwAccel hw) {
    switch (hw) {
        case HwAccel::QSV:   return "h264_qsv";
        case HwAccel::VAAPI: return "h264_vaapi";
        default:             return "libx264";
    }
}

}

SubStreamEncoder::SubStreamEncoder()  = default;
SubStreamEncoder::~SubStreamEncoder() { close(); }

bool SubStreamEncoder::open(const CameraConfig& cam, AVCodecContext* decoder_ctx, HlsMuxer* muxer) {
    close();
    if (!decoder_ctx || !muxer) return false;

    out_w_   = cam.sub_width  > 0 ? cam.sub_width  : 640;
    out_h_   = cam.sub_height > 0 ? cam.sub_height : 360;
    out_fps_ = cam.sub_fps    > 0 ? cam.sub_fps    : 15;

    auto enc_name = pickEncoderName(cam.preferred_hw);
    const AVCodec* enc = avcodec_find_encoder_by_name(enc_name);
    if (!enc) enc      = avcodec_find_encoder_by_name("libx264");
    if (!enc) { NVR_ERROR("sub-enc", "no encoder available"); return false; }

    cc_ = avcodec_alloc_context3(enc);
    if (!cc_) return false;

    cc_->width      = out_w_;
    cc_->height     = out_h_;
    cc_->time_base  = AVRational{1, out_fps_};
    cc_->framerate  = AVRational{out_fps_, 1};
    cc_->gop_size   = out_fps_ * 2;
    cc_->max_b_frames = 0;
    cc_->bit_rate   = static_cast<int64_t>(cam.sub_bitrate_kbps) * 1000;
    cc_->pix_fmt    = (cam.preferred_hw == HwAccel::None) ? AV_PIX_FMT_YUV420P
                                                          : AV_PIX_FMT_NV12;

    if (avcodec_open2(cc_, enc, nullptr) < 0) {
        NVR_ERROR("sub-enc", "open codec '%s' failed", enc_name);
        close();
        return false;
    }

    scaled_ = av_frame_alloc();
    scaled_->format = cc_->pix_fmt;
    scaled_->width  = out_w_;
    scaled_->height = out_h_;
    if (av_frame_get_buffer(scaled_, 32) < 0) {
        NVR_ERROR("sub-enc", "av_frame_get_buffer failed");
        close();
        return false;
    }

    muxer_ = muxer;
    NVR_INFO("sub-enc", "opened: %dx%d @%d fps, %d kbps, codec=%s",
             out_w_, out_h_, out_fps_, cam.sub_bitrate_kbps, enc_name);
    return true;
}

bool SubStreamEncoder::encode(AVFrame* in_frame) {
    if (!cc_ || !muxer_ || !in_frame) return false;

    auto src_fmt = static_cast<AVPixelFormat>(in_frame->format);
    if (sws_src_fmt_ != src_fmt || sws_src_w_ != in_frame->width
        || sws_src_h_ != in_frame->height) {
        if (sws_) { sws_freeContext(sws_); sws_ = nullptr; }
        sws_ = sws_getContext(in_frame->width, in_frame->height, src_fmt,
                              out_w_, out_h_, cc_->pix_fmt,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
        sws_src_fmt_ = src_fmt; sws_src_w_ = in_frame->width; sws_src_h_ = in_frame->height;
    }
    if (!sws_) return false;

    if (av_frame_make_writable(scaled_) < 0) return false;
    sws_scale(sws_, in_frame->data, in_frame->linesize, 0, in_frame->height,
              scaled_->data, scaled_->linesize);
    scaled_->pts = pts_++;

    int err = avcodec_send_frame(cc_, scaled_);
    if (err < 0 && err != AVERROR(EAGAIN)) {
        NVR_WARN("sub-enc", "send_frame: %s", ffErr(err).c_str());
        return false;
    }

    AVPacket* pkt = av_packet_alloc();
    while ((err = avcodec_receive_packet(cc_, pkt)) == 0) {
        muxer_->writePacket(pkt, cc_->time_base);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return true;
}

void SubStreamEncoder::flush() {
    if (!cc_) return;
    avcodec_send_frame(cc_, nullptr);
    AVPacket* pkt = av_packet_alloc();
    while (avcodec_receive_packet(cc_, pkt) == 0) {
        if (muxer_) muxer_->writePacket(pkt, cc_->time_base);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
}

void SubStreamEncoder::close() {
    if (sws_) { sws_freeContext(sws_); sws_ = nullptr; }
    if (scaled_) av_frame_free(&scaled_);
    if (cc_)     avcodec_free_context(&cc_);
    muxer_ = nullptr;
    pts_   = 0;
    sws_src_fmt_ = AV_PIX_FMT_NONE;
    sws_src_w_ = sws_src_h_ = 0;
}

}
