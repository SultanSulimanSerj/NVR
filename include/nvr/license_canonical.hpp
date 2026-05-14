#pragma once

#include <cstdint>
#include <string>

namespace nvr::license_detail {

/// Canonical UTF-8 message signed with Ed25519 (must match [docs/LICENSE_TICKET.md](docs/LICENSE_TICKET.md)).
inline std::string canonicalLicensePayloadString(const std::string& fp, int64_t exp, int max_ch,
                                                 uint32_t mod) {
    return std::string("{\"exp\":") + std::to_string(exp) + ",\"fp\":\"" + fp + "\",\"max_ch\":" +
           std::to_string(max_ch) + ",\"mod\":" + std::to_string(static_cast<unsigned long long>(mod)) +
           "}";
}

} // namespace nvr::license_detail
