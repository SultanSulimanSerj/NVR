#pragma once

#include <filesystem>
#include <memory>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace nvr {

class HlsMuxer {
public:
    HlsMuxer();
    ~HlsMuxer();

    bool open(const std::filesystem::path& dir, AVCodecContext* enc_ctx,
              int segment_seconds = 1, int list_size = 4);

    bool writePacket(AVPacket* pkt, AVRational src_tb);
    void close();

    bool isOpen() const noexcept { return out_ != nullptr; }

private:
    AVFormatContext*       out_{nullptr};
    AVStream*              out_stream_{nullptr};
    AVRational             in_tb_{0, 1};
    std::filesystem::path  dir_;
};

}
