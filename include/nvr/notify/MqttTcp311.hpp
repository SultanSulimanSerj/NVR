#pragma once

#include <string>

namespace nvr::notify {

/// Minimal MQTT 3.1.1 QoS0 PUBLISH over plain TCP (no TLS). Used when `mqtt_delivery` is live.
bool mqttTcpPublishQos0(const std::string& broker_host, int broker_port, const std::string& username,
                         const std::string& password, const std::string& topic, const std::string& payload,
                         int timeout_sec = 8);

} // namespace nvr::notify
