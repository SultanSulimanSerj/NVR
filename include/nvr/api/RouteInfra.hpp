#pragma once

#include "nvr/CameraSupervisor.hpp"
#include "nvr/api/Auth.hpp"
#include "nvr/store/ConfigStore.hpp"

#include <crow.h>

namespace nvr::api {

void register_infra_health_metrics_branding_routes(
    crow::SimpleApp&          app,
    store::ConfigStore&       store,
    Auth&                     auth,
    CameraSupervisor*        supervisor);

} // namespace nvr::api
