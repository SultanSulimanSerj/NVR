#pragma once

#include <crow.h>
#include <functional>
#include <optional>

#include "nvr/api/Auth.hpp"
#include "nvr/store/ConfigStore.hpp"

namespace nvr::api {

/// Registers `/api/v1/events*`, `/api/v1/archive/*` (including timeline).
void register_archive_event_routes(
    crow::SimpleApp&              app,
    store::ConfigStore&           store,
    std::function<std::optional<Identity>(const crow::request&, Role)> require_auth,
    std::function<std::optional<Identity>(const crow::request&, Role)> require_auth_stream);

} // namespace nvr::api
