#pragma once

#include "nvr/api/Auth.hpp"
#include "nvr/store/ConfigStore.hpp"

#include <crow.h>
#include <functional>
#include <optional>

namespace nvr::api {

void register_auth_routes(
    crow::SimpleApp&                                                    app,
    Auth&                                                               auth,
    store::ConfigStore&                                                 store,
    std::function<std::optional<Identity>(const crow::request&, Role)> require_auth,
    std::function<bool(const std::string& ip)>                         rate_limit_login);

} // namespace nvr::api
