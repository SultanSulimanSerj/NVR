#pragma once

#include "nvr/api/Auth.hpp"
#include "nvr/store/ConfigStore.hpp"

#include <crow.h>
#include <functional>
#include <optional>

namespace nvr::api {

/// Live grid presets per user + admin field-support bundle (telemetry opt-in snapshot).
void register_live_layout_and_field_routes(
    crow::SimpleApp&                                                    app,
    store::ConfigStore&                                                 store,
    CameraSupervisor*                                                   supervisor,
    std::function<std::optional<Identity>(const crow::request&, Role)> require_auth);

} // namespace nvr::api
