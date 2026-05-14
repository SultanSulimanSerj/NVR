#pragma once

#include "nvr/onvif/Types.hpp"

#include <chrono>
#include <optional>
#include <string>

namespace nvr::onvif {

class SoapClient {
public:
    SoapClient(std::string endpoint, std::string user, std::string password);

    std::optional<std::string> call(const std::string& action,
                                    const std::string& body_xml);

    std::optional<DeviceInfo>          getDeviceInformation();
    /// Raw XML from `GetCapabilities` (All categories) — used to resolve Events XAddr.
    std::optional<std::string>         getCapabilitiesXml();
    /// Events service (different xmlns in envelope than media/device).
    std::optional<std::string>         callEvents(const std::string& events_endpoint,
                                                  const std::string& action,
                                                  const std::string& body_xml);
    std::vector<Profile>               getProfiles(const std::string& media_endpoint);
    std::optional<std::string>         getStreamUri(const std::string& media_endpoint,
                                                    const std::string& profile_token);
    bool ptzMove(const std::string& ptz_endpoint, const std::string& profile_token,
                  const PtzVector& v);
    bool ptzStop(const std::string& ptz_endpoint, const std::string& profile_token);

    /// Best-effort `SetSystemDateAndTime` (UTC manual). Some firmware rejects or requires NTP.
    bool setSystemDateAndTimeUtc(std::chrono::system_clock::time_point utc_tp);

private:
    std::string endpoint_;
    std::string user_;
    std::string password_;

    std::optional<std::string> postEnvelope_(const std::string& envelope, const std::string& soap_action);
};

}
