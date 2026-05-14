#include "nvr/CameraSupervisor.hpp"

#include "nvr/Logger.hpp"

namespace nvr {

CameraSupervisor::CameraSupervisor(store::ConfigStore&            cfg_store,
                                    ArchiveManager*                archive,
                                    ThreadSafeQueue<MotionEvent>*  motion_queue,
                                    EventBus*                      event_bus,
                                    bool                           hook_include_frame,
                                    std::string                    hls_root)
    : cfg_store_(cfg_store),
      archive_(archive),
      motion_queue_(motion_queue),
      event_bus_(event_bus),
      hook_include_frame_(hook_include_frame),
      hls_root_(std::move(hls_root)) {
    cfg_store_.addCameraListener([this](const store::CameraChange& ch) {
        onCameraChange(ch);
    });
}

void CameraSupervisor::start() {
    auto motion_cfg = cfg_store_.motionConfig();
    auto cams       = cfg_store_.listCameras();
    NVR_INFO("supervisor", "starting with %zu camera(s)", cams.size());

    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& c : cams) launchCamera(c);
    (void)motion_cfg;
}

void CameraSupervisor::stop() {
    std::map<std::string, std::unique_ptr<CameraPipeline>> taken;
    {
        std::lock_guard<std::mutex> lk(mu_);
        taken = std::move(pipelines_);
    }
    for (auto& [id, p] : taken) p->stop();
}

std::vector<std::string> CameraSupervisor::activeCameras() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> ids;
    ids.reserve(pipelines_.size());
    for (const auto& [id, _] : pipelines_) ids.push_back(id);
    return ids;
}

void CameraSupervisor::launchCamera(const CameraConfig& cfg) {
    auto motion_cfg = cfg_store_.motionConfig();
    auto p = std::make_unique<CameraPipeline>(cfg, archive_, motion_cfg,
                                                hook_include_frame_, motion_queue_, event_bus_,
                                                hls_root_);
    p->start();
    pipelines_.emplace(cfg.id, std::move(p));
    if (event_bus_) {
        event_bus_->publish({cfg.id, "camera.started", "info"});
    }
    NVR_INFO("supervisor", "camera '%s' launched", cfg.id.c_str());
}

void CameraSupervisor::stopCamera(const std::string& id) {
    auto it = pipelines_.find(id);
    if (it == pipelines_.end()) return;
    it->second->stop();
    pipelines_.erase(it);
    if (event_bus_) {
        event_bus_->publish({id, "camera.stopped", "info"});
    }
    NVR_INFO("supervisor", "camera '%s' stopped", id.c_str());
}

void CameraSupervisor::onCameraChange(const store::CameraChange& ch) {
    std::lock_guard<std::mutex> lk(mu_);
    switch (ch.kind) {
        case store::CameraChangeKind::Added:
            launchCamera(ch.camera);
            break;
        case store::CameraChangeKind::Updated:
            stopCamera(ch.camera.id);
            launchCamera(ch.camera);
            break;
        case store::CameraChangeKind::Removed:
            stopCamera(ch.camera.id);
            break;
    }
}

}
