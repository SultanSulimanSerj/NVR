#pragma once

#include "nvr/onvif/Types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace nvr::onvif {

struct EnumeratedStream {
    std::string profile_token;
    std::string profile_name;
    int         width{0};
    int         height{0};
    std::string codec;
    std::string uri;
};

struct EnumerateStreamsResult {
    bool        ok{false};
    std::string error;
    /// Effective device service URL used for SOAP.
    std::string device_service_url;
    std::string media_service_url;
    std::optional<DeviceInfo> device;
    std::vector<EnumeratedStream> streams;
    /// Parsed from `device_service_url` for UI defaults.
    std::string onvif_host;
    int         onvif_port{80};
};

/// Resolves Media service (GetCapabilities or path heuristic), lists profiles, fetches RTSP URIs.
EnumerateStreamsResult enumerateStreams(const std::string& device_service_url,
                                         const std::string& user,
                                         const std::string& pass);

} // namespace nvr::onvif
