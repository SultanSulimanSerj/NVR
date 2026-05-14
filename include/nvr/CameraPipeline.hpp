#pragma once

#include "nvr/ArchiveManager.hpp"
#include "nvr/Config.hpp"
#include "nvr/FFmpegDecoder.hpp"
#include "nvr/FFmpegEncoder.hpp"
#include "nvr/HlsMuxer.hpp"
#include "nvr/MotionDetector.hpp"
#include "nvr/MotionEvent.hpp"
#include "nvr/SubStreamEncoder.hpp"
#include "nvr/ThreadSafeQueue.hpp"

#include <atomic>
#include <memory>
#include <thread>

namespace nvr {

class EventBus;

class CameraPipeline {
public:
    CameraPipeline(CameraConfig         cam,
                   ArchiveManager*      archive,
                   MotionConfig         motion_cfg,
                   bool                 hook_include_frame,
                   ThreadSafeQueue<MotionEvent>* motion_queue,
                   EventBus*            event_bus,
                   std::string          hls_root = {});

    ~CameraPipeline();

    CameraPipeline(const CameraPipeline&)            = delete;
    CameraPipeline& operator=(const CameraPipeline&) = delete;

    void start();
    void stop();

    const std::string& id() const noexcept { return cam_.id; }

private:
    void run();
    void runOnce();

    CameraConfig                    cam_;
    ArchiveManager*                 archive_{nullptr};
    MotionConfig                    motion_cfg_;
    bool                            hook_include_frame_{false};
    ThreadSafeQueue<MotionEvent>*   motion_queue_{nullptr};
    EventBus*                       event_bus_{nullptr};
    std::string                     hls_root_;

    std::thread                     worker_;
    std::atomic<bool>               running_{false};
};

}
