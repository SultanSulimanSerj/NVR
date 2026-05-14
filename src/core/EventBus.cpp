#include "nvr/EventBus.hpp"
#include "nvr/Logger.hpp"

#include <algorithm>

namespace nvr {

uint64_t EventBus::subscribe(Sub cb) {
    std::lock_guard<std::mutex> lk(mu_);
    auto id = seq_.fetch_add(1);
    subs_.push_back({id, std::move(cb)});
    return id;
}

void EventBus::unsubscribe(uint64_t id) {
    std::lock_guard<std::mutex> lk(mu_);
    subs_.erase(std::remove_if(subs_.begin(), subs_.end(),
                                [&](const Entry& e) { return e.id == id; }),
                 subs_.end());
}

void EventBus::publish(SystemEvent ev) {
    std::vector<Entry> snapshot;
    {
        std::lock_guard<std::mutex> lk(mu_);
        snapshot = subs_;
    }
    for (auto& e : snapshot) {
        try { e.cb(ev); }
        catch (const std::exception& ex) {
            NVR_WARN("eventbus", "subscriber threw: %s", ex.what());
        }
    }
}

}
