#pragma once

#include "nvr/Config.hpp"
#include "nvr/MotionEvent.hpp"
#include "nvr/ThreadSafeQueue.hpp"

#include <atomic>
#include <memory>
#include <thread>

namespace nvr {

class PythonHookManager {
public:
    explicit PythonHookManager(PythonConfig cfg);
    ~PythonHookManager();

    PythonHookManager(const PythonHookManager&)            = delete;
    PythonHookManager& operator=(const PythonHookManager&) = delete;

    void start();
    void stop();

    ThreadSafeQueue<MotionEvent>& queue() noexcept { return queue_; }

private:
    void workerLoop();
    void dispatch(const MotionEvent& ev);

    PythonConfig                 cfg_;
    ThreadSafeQueue<MotionEvent> queue_;
    std::thread                  worker_;
    std::atomic<bool>            running_{false};

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
