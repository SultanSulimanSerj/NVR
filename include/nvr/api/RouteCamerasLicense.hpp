#pragma once

#include "nvr/CameraSupervisor.hpp"
#include "nvr/EventBus.hpp"
#include "nvr/LicenseGate.hpp"
#include "nvr/api/Auth.hpp"
#include "nvr/store/ConfigStore.hpp"

#include <crow.h>
#include <functional>
#include <optional>

namespace nvr::api {

void register_cameras_license_routes(
    crow::SimpleApp&                                                    app,
    store::ConfigStore&                                                 store,
    CameraSupervisor*                                                   supervisor,
    const LicenseGate&                                                  license_gate,
    EventBus*                                                           event_bus,
    std::function<std::optional<Identity>(const crow::request&, Role)> require_auth);

} // namespace nvr::api
