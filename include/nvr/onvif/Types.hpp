#pragma once

#include <string>
#include <vector>

namespace nvr::onvif {

struct Discovered {
    std::string endpoint;
    std::string xaddrs;
    std::string types;
    std::string scopes;
};

struct Profile {
    std::string token;
    std::string name;
    int         width{0};
    int         height{0};
    std::string codec;
    std::string ptz_token;
};

struct StreamUri {
    std::string uri;
};

struct DeviceInfo {
    std::string manufacturer;
    std::string model;
    std::string firmware;
    std::string serial;
};

struct PtzVector { double pan{0}, tilt{0}, zoom{0}; };

}
