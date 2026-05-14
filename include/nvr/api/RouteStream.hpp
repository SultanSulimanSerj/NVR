#pragma once

#include "nvr/Config.hpp"
#include "nvr/api/Auth.hpp"
#include "nvr/store/ConfigStore.hpp"

#include <crow.h>
#include <crow/websocket.h>
#include <functional>
#include <mutex>
#include <optional>
#include <set>

namespace nvr::api {

/// HLS live segments under `/live/<camera>/<file>` and `/api/v1/ws` fan-out.
void register_stream_routes(
    crow::SimpleApp&                                                    app,
    const HttpConfig&                                                   cfg,
    store::ConfigStore&                                                 store,
    Auth&                                                               auth,
    std::mutex&                                                         ws_mu,
    std::set<crow::websocket::connection*>&                             ws_conns,
    std::function<std::optional<Identity>(const crow::request&, Role)> require_auth_stream);

} // namespace nvr::api
