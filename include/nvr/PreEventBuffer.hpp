#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <chrono>
#include <deque>
#include <mutex>

namespace nvr {

struct BufferedPacket {
    AVPacket* pkt;
    std::chrono::steady_clock::time_point ts;
};

class PreEventBuffer {
public:
    explicit PreEventBuffer(std::chrono::seconds window);
    ~PreEventBuffer();

    PreEventBuffer(const PreEventBuffer&) = delete;
    PreEventBuffer& operator=(const PreEventBuffer&) = delete;

    void push(AVPacket* src);
    void drainOlderThan(std::chrono::steady_clock::time_point cutoff);
    std::vector<AVPacket*> snapshotRefs() const;

    void clear();

    std::chrono::seconds window() const noexcept { return window_; }

private:
    std::chrono::seconds      window_;
    mutable std::mutex        mu_;
    std::deque<BufferedPacket> q_;
};

}
