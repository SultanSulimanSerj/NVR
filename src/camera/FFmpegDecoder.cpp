#include "nvr/FFmpegDecoder.hpp"

#include "nvr/Logger.hpp"

#include <mutex>

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/error.h>
}

namespace nvr {

void AVPacketDeleter::operator()(AVPacket* p) const noexcept {
    if (p) av_packet_free(&p);
}
void AVFrameDeleter::operator()(AVFrame* p) const noexcept {
    if (p) av_frame_free(&p);
}
void AVFormatInputDeleter::operator()(AVFormatContext* p) const noexcept {
    if (p) avformat_close_input(&p);
}
void AVCodecCtxDeleter::operator()(AVCodecContext* p) const noexcept {
    if (p) avcodec_free_context(&p);
}
void AVBufferRefDeleter::operator()(AVBufferRef* p) const noexcept {
    if (p) av_buffer_unref(&p);
}

namespace {

std::once_flag g_init_once;

std::string ffErr(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, buf, sizeof(buf));
    return buf;
}

AVHWDeviceType toAvType(HwAccel hw) {
    switch (hw) {
        case HwAccel::QSV:   return AV_HWDEVICE_TYPE_QSV;
        case HwAccel::VAAPI: return AV_HWDEVICE_TYPE_VAAPI;
        default:             return AV_HWDEVICE_TYPE_NONE;
    }
}

// Per-instance target hardware pixel format. We stash it in AVCodecContext::opaque
// so multiple cameras can negotiate different formats concurrently without
// stepping on each other via a global.
AVPixelFormat getHwFormatCb(AVCodecContext* ctx, const AVPixelFormat* fmts) {
    auto target = static_cast<AVPixelFormat>(
        reinterpret_cast<intptr_t>(ctx->opaque));
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == target) return *p;
    }
    return fmts[0];
}

}

FFmpegDecoder::FFmpegDecoder()  { initFFmpeg(); }
FFmpegDecoder::~FFmpegDecoder() { closeAll(); }

void FFmpegDecoder::initFFmpeg() {
    std::call_once(g_init_once, [] {
        avformat_network_init();
        av_log_set_level(AV_LOG_WARNING);
    });
}

void FFmpegDecoder::closeAll() {
    cc_.reset();
    in_.reset();
    hw_device_ctx_.reset();
    video_stream_idx_ = -1;
    active_           = HwAccel::None;
}

bool FFmpegDecoder::initHwDevice(AVHWDeviceType type) {
    AVBufferRef* dev = nullptr;
    int err = av_hwdevice_ctx_create(&dev, type, nullptr, nullptr, 0);
    if (err < 0) {
        NVR_WARN("decoder", "av_hwdevice_ctx_create(%s) failed: %s",
                 av_hwdevice_get_type_name(type), ffErr(err).c_str());
        return false;
    }
    hw_device_ctx_.reset(dev);
    return true;
}

namespace {

struct HwCodecChoice {
    AVHWDeviceType type;
    const char*    h264_name;
    const char*    hevc_name;
    AVPixelFormat  pix_fmt;
};

HwCodecChoice hwChoiceFor(HwAccel hw) {
    switch (hw) {
        case HwAccel::QSV:
            return {AV_HWDEVICE_TYPE_QSV,   "h264_qsv",   "hevc_qsv",   AV_PIX_FMT_QSV};
        case HwAccel::VAAPI:
            return {AV_HWDEVICE_TYPE_VAAPI, "h264_vaapi", "hevc_vaapi", AV_PIX_FMT_VAAPI};
        default:
            return {AV_HWDEVICE_TYPE_NONE,  nullptr,      nullptr,      AV_PIX_FMT_NONE};
    }
}

const AVCodec* pickDecoder(HwAccel hw, AVCodecID codec_id) {
    auto choice = hwChoiceFor(hw);
    if (choice.type != AV_HWDEVICE_TYPE_NONE) {
        const char* name = (codec_id == AV_CODEC_ID_HEVC) ? choice.hevc_name : choice.h264_name;
        if (name) {
            if (const AVCodec* c = avcodec_find_decoder_by_name(name)) return c;
        }
    }
    return avcodec_find_decoder(codec_id);
}

}

bool FFmpegDecoder::open(const std::string& rtsp_url, HwAccel preferred) {
    static constexpr HwAccel kAutoOrder[] = {HwAccel::QSV, HwAccel::VAAPI, HwAccel::None};

    auto attempt = [&](HwAccel hw) {
        closeAll();
        if (tryOpenWith(hw)) {
            NVR_INFO("decoder", "opened '%s' with hwaccel=%s",
                     rtsp_url.c_str(), hwAccelToString(hw));
            active_ = hw;
            return true;
        }
        return false;
    };

    rtsp_url_ = rtsp_url;
    if (preferred == HwAccel::Auto) {
        for (HwAccel hw : kAutoOrder) {
            if (attempt(hw)) return true;
        }
        NVR_ERROR("decoder", "failed to open '%s' with any backend", rtsp_url.c_str());
        return false;
    }
    if (attempt(preferred)) return true;

    NVR_WARN("decoder", "preferred hwaccel '%s' failed, falling back to software",
             hwAccelToString(preferred));
    return attempt(HwAccel::None);
}

bool FFmpegDecoder::tryOpenWith(HwAccel hw) {
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&opts, "stimeout",       "5000000", 0);
    av_dict_set(&opts, "timeout",        "5000000", 0);
    av_dict_set(&opts, "max_delay",      "500000", 0);
    av_dict_set(&opts, "buffer_size",    "1048576", 0);
    av_dict_set(&opts, "reorder_queue_size", "1024", 0);
    av_dict_set(&opts, "fflags",         "nobuffer+discardcorrupt", 0);
    av_dict_set(&opts, "flags",          "low_delay", 0);
    av_dict_set(&opts, "probesize",      "200000", 0);
    av_dict_set(&opts, "analyzeduration","200000", 0);

    AVFormatContext* in_raw = nullptr;
    int err = avformat_open_input(&in_raw, rtsp_url_.c_str(), nullptr, &opts);
    av_dict_free(&opts);
    if (err < 0) {
        NVR_WARN("decoder", "avformat_open_input('%s'): %s",
                 rtsp_url_.c_str(), ffErr(err).c_str());
        return false;
    }
    in_.reset(in_raw);

    err = avformat_find_stream_info(in_.get(), nullptr);
    if (err < 0) {
        NVR_WARN("decoder", "avformat_find_stream_info: %s", ffErr(err).c_str());
        return false;
    }

    int v_idx = av_find_best_stream(in_.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (v_idx < 0) {
        NVR_WARN("decoder", "no video stream found");
        return false;
    }
    video_stream_idx_ = v_idx;

    AVStream*       vs    = in_->streams[v_idx];
    AVCodecParameters* par = vs->codecpar;

    const AVCodec* dec = pickDecoder(hw, par->codec_id);
    if (!dec) {
        NVR_WARN("decoder", "no suitable decoder for codec_id=%d (hw=%s)",
                 par->codec_id, hwAccelToString(hw));
        return false;
    }

    AVCodecContext* cc = avcodec_alloc_context3(dec);
    if (!cc) return false;
    cc_.reset(cc);

    if (avcodec_parameters_to_context(cc, par) < 0) {
        NVR_WARN("decoder", "avcodec_parameters_to_context failed");
        return false;
    }
    cc->pkt_timebase = vs->time_base;

    auto choice = hwChoiceFor(hw);
    if (choice.type != AV_HWDEVICE_TYPE_NONE) {
        if (!initHwDevice(choice.type)) return false;
        AVBufferRef* hw_ref = av_buffer_ref(hw_device_ctx_.get());
        if (!hw_ref) {
            NVR_WARN("decoder", "av_buffer_ref failed");
            return false;
        }
        cc->hw_device_ctx = hw_ref;
        cc->opaque        = reinterpret_cast<void*>(static_cast<intptr_t>(choice.pix_fmt));
        cc->get_format    = getHwFormatCb;
    }

    err = avcodec_open2(cc, dec, nullptr);
    if (err < 0) {
        NVR_WARN("decoder", "avcodec_open2(%s): %s",
                 dec->name, ffErr(err).c_str());
        return false;
    }

    NVR_DEBUG("decoder",
              "stream=%d codec=%s %dx%d fps=%.2f tb=%d/%d hw=%s",
              v_idx, dec->name, cc->width, cc->height,
              vs->avg_frame_rate.den ? av_q2d(vs->avg_frame_rate) : 0.0,
              vs->time_base.num, vs->time_base.den, hwAccelToString(hw));
    return true;
}

PacketPtr FFmpegDecoder::readPacket(int& result) {
    PacketPtr pkt(av_packet_alloc());
    if (!pkt) { result = AVERROR(ENOMEM); return {}; }
    result = av_read_frame(in_.get(), pkt.get());
    return pkt;
}

bool FFmpegDecoder::sendPacket(AVPacket* pkt) {
    int err = avcodec_send_packet(cc_.get(), pkt);
    if (err < 0 && err != AVERROR(EAGAIN) && err != AVERROR_EOF) {
        NVR_WARN("decoder", "avcodec_send_packet: %s", ffErr(err).c_str());
        return false;
    }
    return true;
}

int FFmpegDecoder::receiveFrame(AVFrame* out) {
    return avcodec_receive_frame(cc_.get(), out);
}

bool FFmpegDecoder::downloadIfHardware(AVFrame* hw_in, AVFrame* sw_out) {
    if (!hw_in || !sw_out) return false;

    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(hw_in->format));
    const bool is_hw = desc && (desc->flags & AV_PIX_FMT_FLAG_HWACCEL);

    if (!is_hw) {
        return av_frame_ref(sw_out, hw_in) == 0;
    }

    int err = av_hwframe_transfer_data(sw_out, hw_in, 0);
    if (err < 0) {
        NVR_WARN("decoder", "av_hwframe_transfer_data failed: %s", ffErr(err).c_str());
        return false;
    }
    sw_out->pts = hw_in->pts;
    return true;
}

}
