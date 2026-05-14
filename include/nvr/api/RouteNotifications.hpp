#pragma once

#include "nvr/api/Auth.hpp"
#include "nvr/EventBus.hpp"
#include "nvr/notify/NotificationManager.hpp"
#include "nvr/store/ConfigStore.hpp"

#include <crow.h>
#include <functional>
#include <optional>

namespace nvr::api {

void register_notification_routes(
    crow::SimpleApp&                                                    app,
    store::ConfigStore&                                                 store,
    notify::NotificationManager*                                       notify_mgr,
    EventBus*                                                           event_bus,
    std::function<std::optional<Identity>(const crow::request&, Role)> require_auth);

} // namespace nvr::api
