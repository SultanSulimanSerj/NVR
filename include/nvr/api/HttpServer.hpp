#pragma once

#include "nvr/CameraSupervisor.hpp"
#include "nvr/Config.hpp"
#include "nvr/EventBus.hpp"
#include "nvr/LicenseGate.hpp"
#include "nvr/api/Auth.hpp"
#include "nvr/store/ConfigStore.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace nvr::notify { class NotificationManager; }

namespace nvr::api {

class HttpServer {
public:
    HttpServer(HttpConfig                  cfg,
               store::ConfigStore&         store,
               Auth&                       auth,
               CameraSupervisor*           supervisor,
               EventBus*                   event_bus,
               notify::NotificationManager* notify_mgr,
               const LicenseGate&          license_gate);
    ~HttpServer();

    void start();
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
