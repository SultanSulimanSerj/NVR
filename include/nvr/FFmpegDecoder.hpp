#pragma once

#include "nvr/Config.hpp"

#include <functional>
#include <memory>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/buffer.h>
#include <libavutil/hwcontext.h>
}

namespace nvr {

struct AVPacketDeleter      { void operator()(AVPacket* p) const noexcept; };
struct AVFrameDeleter       { void operator()(AVFrame* p) const noexcept; };
struct AVFormatInputDeleter { void operator()(AVFormatContext* p) const noexcept; };
struct AVCodecCtxDeleter    { void operator()(AVCodecContext* p) const noexcept; };
struct AVBufferRefDeleter   { void operator()(AVBufferRef* p) const noexcept; };

using PacketPtr   = std::unique_ptr<AVPacket,         AVPacketDeleter>;
using FramePtr    = std::unique_ptr<AVFrame,          AVFrameDeleter>;
using InputPtr    = std::unique_ptr<AVFormatContext,  AVFormatInputDeleter>;
using CodecCtxPtr = std::unique_ptr<AVCodecContext,   AVCodecCtxDeleter>;
using BufferRef   = std::unique_ptr<AVBufferRef,      AVBufferRefDeleter>;

class FFmpegDecoder {
public:
    FFmpegDecoder();
    ~FFmpegDecoder();

    bool open(const std::string& rtsp_url, HwAccel preferred);

    AVFormatContext* inputCtx() const noexcept { return in_.get(); }
    AVCodecContext*  codecCtx() const noexcept { return cc_.get(); }
    int              videoStreamIndex() const noexcept { return video_stream_idx_; }
    HwAccel          activeHwAccel() const noexcept { return active_; }

    PacketPtr        readPacket(int& result);
    bool             sendPacket(AVPacket* pkt);
    int              receiveFrame(AVFrame* out);
    bool             downloadIfHardware(AVFrame* hw_in, AVFrame* sw_out);

    static void      initFFmpeg();

private:
    bool tryOpenWith(HwAccel hw);
    bool initHwDevice(AVHWDeviceType type);
    void closeAll();

    InputPtr     in_{};
    CodecCtxPtr  cc_{};
    BufferRef    hw_device_ctx_{};
    int          video_stream_idx_{-1};
    HwAccel      active_{HwAccel::None};
    std::string  rtsp_url_{};
};

}
