#pragma once

#include "nvr/Config.hpp"

#include <filesystem>
#include <memory>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

namespace nvr {

class HlsMuxer;

class SubStreamEncoder {
public:
    SubStreamEncoder();
    ~SubStreamEncoder();

    bool open(const CameraConfig& cam, AVCodecContext* decoder_ctx, HlsMuxer* muxer);

    bool encode(AVFrame* in_frame);

    void flush();
    void close();

    bool isOpen() const noexcept { return cc_ != nullptr; }
    AVCodecContext* codecContext() const noexcept { return cc_; }

private:
    AVCodecContext* cc_{nullptr};
    SwsContext*     sws_{nullptr};
    AVFrame*        scaled_{nullptr};
    HlsMuxer*       muxer_{nullptr};
    int64_t         pts_{0};
    int             out_w_{0}, out_h_{0}, out_fps_{15};
    AVPixelFormat   sws_src_fmt_{AV_PIX_FMT_NONE};
    int             sws_src_w_{0}, sws_src_h_{0};
};

}
