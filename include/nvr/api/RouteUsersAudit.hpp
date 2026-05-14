#pragma once

#include "nvr/api/Auth.hpp"
#include "nvr/store/ConfigStore.hpp"

#include <crow.h>
#include <functional>
#include <optional>

namespace nvr::api {

void register_users_audit_routes(
    crow::SimpleApp&                                                    app,
    Auth&                                                               auth,
    store::ConfigStore&                                                 store,
    std::function<std::optional<Identity>(const crow::request&, Role)> require_auth);

} // namespace nvr::api
