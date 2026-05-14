#pragma once

#include "nvr/EventBus.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>

namespace nvr::notify {

class INotificationChannel {
public:
    virtual ~INotificationChannel() = default;
    virtual std::string kind() const = 0;
    virtual bool send(const SystemEvent& ev) = 0;
};

}
