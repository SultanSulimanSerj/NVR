#include "nvr/ai/Tracker.hpp"

#include <algorithm>

namespace nvr::ai {

namespace {

float iou(const BBox& a, const BBox& b) {
    float xa = std::max(a.x, b.x);
    float ya = std::max(a.y, b.y);
    float xb = std::min(a.x + a.w, b.x + b.w);
    float yb = std::min(a.y + a.h, b.y + b.h);
    float w  = std::max(0.f, xb - xa);
    float h  = std::max(0.f, yb - ya);
    float i  = w * h;
    float u  = a.w * a.h + b.w * b.h - i;
    return u > 0 ? i / u : 0.f;
}

}

IouTracker::IouTracker(float iou_thr, int max_age) : iou_thr_(iou_thr), max_age_(max_age) {}

void IouTracker::assign(std::vector<Detection>& dets) {
    std::vector<bool> det_used(dets.size(), false);
    std::vector<bool> trk_used(tracks_.size(), false);

    for (size_t ti = 0; ti < tracks_.size(); ++ti) {
        float best = iou_thr_;
        size_t best_di = static_cast<size_t>(-1);
        for (size_t di = 0; di < dets.size(); ++di) {
            if (det_used[di]) continue;
            float v = iou(tracks_[ti].bbox, dets[di].bbox);
            if (v > best) { best = v; best_di = di; }
        }
        if (best_di != static_cast<size_t>(-1)) {
            tracks_[ti].bbox = dets[best_di].bbox;
            tracks_[ti].age++;
            tracks_[ti].missed = 0;
            dets[best_di].track_id = tracks_[ti].id;
            det_used[best_di] = true;
            trk_used[ti]      = true;
        }
    }

    for (size_t ti = 0; ti < tracks_.size(); ++ti) {
        if (!trk_used[ti]) tracks_[ti].missed++;
    }
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
                                   [&](const Track& t) { return t.missed > max_age_; }),
                   tracks_.end());

    for (size_t di = 0; di < dets.size(); ++di) {
        if (det_used[di]) continue;
        Track nt;
        nt.id   = next_id_++;
        nt.bbox = dets[di].bbox;
        nt.age  = 1;
        tracks_.push_back(nt);
        dets[di].track_id = nt.id;
    }
}

}
