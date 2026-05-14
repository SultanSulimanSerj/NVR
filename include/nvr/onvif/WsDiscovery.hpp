#pragma once

#include "nvr/onvif/Types.hpp"

#include <chrono>
#include <vector>

namespace nvr::onvif {

std::vector<Discovered> probe(std::chrono::milliseconds timeout = std::chrono::seconds{3});

}
