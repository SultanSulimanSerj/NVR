#include "nvr/notify/Channels.hpp"
#include "nvr/notify/MqttTcp311.hpp"
#include "nvr/Logger.hpp"
#include "nvr/store/ConfigStore.hpp"

#include <cctype>
#include <nlohmann/json.hpp>

namespace nvr::notify {

namespace {

void parseBroker(const nlohmann::json& cfg, std::string& host, int& port) {
    host = cfg.value("broker_host", std::string{});
    if (!host.empty()) {
        port = cfg.value("broker_port", 1883);
        return;
    }
    const std::string b = cfg.value("broker", std::string{});
    if (b.empty()) return;
    const auto colon = b.find_last_of(':');
    if (colon != std::string::npos && colon > 0 && colon + 1 < b.size()) {
        bool digits = true;
        for (size_t i = colon + 1; i < b.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(b[i]))) {
                digits = false;
                break;
            }
        }
        if (digits) {
            host = b.substr(0, colon);
            try {
                port = std::stoi(b.substr(colon + 1));
            } catch (...) {
                port = 1883;
            }
            return;
        }
    }
    host = b;
    port = 1883;
}

class MqttChannel : public INotificationChannel {
public:
    MqttChannel(nlohmann::json cfg, store::ConfigStore* store)
        : cfg_(std::move(cfg)), store_(store) {}

    std::string kind() const override { return "mqtt"; }

    bool send(const SystemEvent& ev) override {
        std::string mode = "stub";
        if (store_) {
            mode = store_->db().getSetting("features.mqtt_delivery", "stub");
        }
        if (mode != "live") {
            NVR_WARN("notify", "mqtt_delivery=%s (set features.mqtt_delivery=live in DB to enable)",
                     mode.c_str());
            return false;
        }

        std::string host;
        int         port = 1883;
        parseBroker(cfg_, host, port);
        if (host.empty()) {
            NVR_WARN("notify", "mqtt: broker_host/broker missing");
            return false;
        }

        std::string topic = cfg_.value("topic", std::string("nvr/events"));
        {
            auto pos = topic.find("{camera_id}");
            if (pos != std::string::npos) topic.replace(pos, 13, ev.camera_id);
        }
        {
            auto pos = topic.find("{type}");
            if (pos != std::string::npos) topic.replace(pos, 6, ev.type);
        }

        nlohmann::json j = {
            {"camera_id",     ev.camera_id},
            {"type",          ev.type},
            {"severity",      ev.severity},
            {"snapshot_path", ev.snapshot_path},
            {"clip_path",     ev.clip_path},
            {"payload",       nlohmann::json::parse(ev.payload_json, nullptr, false)},
        };
        const std::string body = j.dump();

        const std::string user = cfg_.value("user", cfg_.value("username", std::string{}));
        const std::string pass = cfg_.value("pass", cfg_.value("password", std::string{}));

        const int tmo = cfg_.value("timeout_sec", 8);
        return mqttTcpPublishQos0(host, port, user, pass, topic, body, tmo);
    }

private:
    nlohmann::json        cfg_;
    store::ConfigStore*   store_{nullptr};
};

} // namespace

std::unique_ptr<INotificationChannel> makeMqtt(const nlohmann::json& cfg, store::ConfigStore* store) {
    return std::make_unique<MqttChannel>(cfg, store);
}

} // namespace nvr::notify
