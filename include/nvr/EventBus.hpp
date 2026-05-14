#pragma once

#include "nvr/MotionEvent.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace nvr {

struct SystemEvent {
    std::string  camera_id;
    std::string  type;
    std::string  severity{"info"};
    std::string  payload_json{"{}"};
    std::string  snapshot_path;
    std::string  clip_path;
    std::chrono::system_clock::time_point ts{std::chrono::system_clock::now()};
};

class EventBus {
public:
    using Sub = std::function<void(const SystemEvent&)>;

    uint64_t subscribe(Sub cb);
    void     unsubscribe(uint64_t id);
    void     publish(SystemEvent ev);

private:
    struct Entry { uint64_t id; Sub cb; };
    std::mutex            mu_;
    std::atomic<uint64_t> seq_{1};
    std::vector<Entry>    subs_;
};

}
