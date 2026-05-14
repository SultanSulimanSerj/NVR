#pragma once

#include "nvr/EventBus.hpp"

#include <atomic>
#include <thread>

namespace nvr::system {

class HwMonitor {
public:
    explicit HwMonitor(EventBus* bus = nullptr);
    ~HwMonitor();
    void start();
    void stop();
private:
    void loop();
    EventBus*           bus_;
    std::thread         t_;
    std::atomic<bool>   run_{false};
};

}
