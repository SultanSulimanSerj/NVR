#pragma once

#include "nvr/ArchiveManager.hpp"
#include "nvr/CameraPipeline.hpp"
#include "nvr/EventBus.hpp"
#include "nvr/MotionEvent.hpp"
#include "nvr/ThreadSafeQueue.hpp"
#include "nvr/store/ConfigStore.hpp"

#include <map>
#include <memory>
#include <mutex>

namespace nvr {

class CameraSupervisor {
public:
    CameraSupervisor(store::ConfigStore&            cfg_store,
                     ArchiveManager*                archive,
                     ThreadSafeQueue<MotionEvent>*  motion_queue,
                     EventBus*                      event_bus,
                     bool                           hook_include_frame,
                     std::string                    hls_root = {});

    void start();
    void stop();

    std::vector<std::string> activeCameras() const;

private:
    void onCameraChange(const store::CameraChange& ch);
    void launchCamera(const CameraConfig& cfg);
    void stopCamera(const std::string& id);

    store::ConfigStore&            cfg_store_;
    ArchiveManager*                archive_;
    ThreadSafeQueue<MotionEvent>*  motion_queue_;
    EventBus*                      event_bus_;
    bool                           hook_include_frame_;
    std::string                    hls_root_;

    mutable std::mutex             mu_;
    std::map<std::string, std::unique_ptr<CameraPipeline>> pipelines_;
};

}
