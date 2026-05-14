#pragma once

#include <optional>
#include <string>
#include <vector>

namespace nvr::onvif {

/// One parsed notification (topic + small XML slice for debugging).
struct PulledOnvifEvent {
    std::string topic;
    std::string raw_snippet;
};

/// Resolve Events service URL: optional override, else `GetCapabilities`, else default path.
std::string resolveEventsServiceUrl(const std::string& host, int port, const std::string& user,
                                     const std::string& pass,
                                     const std::optional<std::string>& events_url_override);

/// CreatePullPointSubscription + one PullMessages; best-effort parse of NotificationMessage blocks.
std::vector<PulledOnvifEvent> pullPointOnce(const std::string& events_service_url,
                                            const std::string& user, const std::string& pass);

} // namespace nvr::onvif
