#include "nvr/PreEventBuffer.hpp"

namespace nvr {

PreEventBuffer::PreEventBuffer(std::chrono::seconds window) : window_(window) {}

PreEventBuffer::~PreEventBuffer() { clear(); }

void PreEventBuffer::push(AVPacket* src) {
    if (!src) return;
    AVPacket* p = av_packet_alloc();
    if (av_packet_ref(p, src) < 0) { av_packet_free(&p); return; }

    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lk(mu_);
    q_.push_back({p, now});

    auto cutoff = now - window_;
    while (!q_.empty() && q_.front().ts < cutoff) {
        av_packet_free(&q_.front().pkt);
        q_.pop_front();
    }
}

void PreEventBuffer::drainOlderThan(std::chrono::steady_clock::time_point cutoff) {
    std::lock_guard<std::mutex> lk(mu_);
    while (!q_.empty() && q_.front().ts < cutoff) {
        av_packet_free(&q_.front().pkt);
        q_.pop_front();
    }
}

std::vector<AVPacket*> PreEventBuffer::snapshotRefs() const {
    std::vector<AVPacket*> out;
    std::lock_guard<std::mutex> lk(mu_);
    out.reserve(q_.size());
    for (const auto& bp : q_) {
        AVPacket* clone = av_packet_alloc();
        if (av_packet_ref(clone, bp.pkt) == 0) out.push_back(clone);
        else av_packet_free(&clone);
    }
    return out;
}

void PreEventBuffer::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    while (!q_.empty()) {
        av_packet_free(&q_.front().pkt);
        q_.pop_front();
    }
}

}
