#pragma once

#include "nvr/api/Auth.hpp"
#include "nvr/Config.hpp"

#include "nvr/store/ConfigStore.hpp"

#include <crow.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace nvr::api {

std::optional<std::string> extractAuthToken(const crow::request& req);
std::optional<std::string> extractStreamToken(const crow::request& req);
bool                        jsonRefreshTokenOptIn(const crow::request& req);
std::string                 clientIp(const crow::request& req);
bool                        roleAtLeast(Role have, Role required);
std::string                 isoNow();
std::string                 mimeFor(const std::filesystem::path& p);
bool                        readFile(const std::filesystem::path& p, std::string& out);
std::string                 cameraToJson(const CameraConfig& c);
CameraConfig                jsonToCamera(const nlohmann::json& j, const std::string& id_override = "");
bool                        isSafeToken(const std::string& s, size_t max_len = 64);
bool                        isSafeHostname(const std::string& s);
bool                        isSafeTimezone(const std::string& s);
bool                        isSafeBlockDevice(const std::string& s);
std::string                 runArgv(const std::vector<std::string>& argv, int max_bytes = 1 << 20);
bool                        runArgvDetached(const std::vector<std::string>& argv);

/// Per-camera ACL: admins always pass; others pass if they have no ACL rows or a matching row.
bool                        cameraVisibleToUser(const store::ConfigStore& store, const Identity& who,
                                                  const std::string& camera_id);

} // namespace nvr::api
