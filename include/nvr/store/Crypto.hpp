#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace nvr::store {

struct MasterKey {
    static constexpr size_t kSize = 32;
    std::array<uint8_t, kSize> bytes{};
};

MasterKey loadOrCreateMasterKey(const std::filesystem::path& path);

std::vector<uint8_t> encrypt(const MasterKey& key, const std::string& plaintext,
                             const std::string& aad = {});

std::optional<std::string> decrypt(const MasterKey& key, const std::vector<uint8_t>& blob,
                                   const std::string& aad = {});

std::string hashPassword(const std::string& plain);
bool        verifyPassword(const std::string& plain, const std::string& hash);

std::string randomHex(size_t bytes);

}
