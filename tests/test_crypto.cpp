#include "nvr/store/Crypto.hpp"

#include <gtest/gtest.h>

TEST(Crypto, RoundtripEncryptDecrypt) {
    nvr::store::MasterKey k;
    for (size_t i = 0; i < k.bytes.size(); ++i) k.bytes[i] = static_cast<uint8_t>(i);

    auto blob  = nvr::store::encrypt(k, "secret-pass", "tag-1");
    auto plain = nvr::store::decrypt(k, blob, "tag-1");
    ASSERT_TRUE(plain.has_value());
    EXPECT_EQ(*plain, "secret-pass");

    auto plain_bad = nvr::store::decrypt(k, blob, "tag-2");
    EXPECT_FALSE(plain_bad.has_value());
}

TEST(Crypto, PasswordHashVerify) {
    auto h = nvr::store::hashPassword("hunter2");
    EXPECT_TRUE (nvr::store::verifyPassword("hunter2", h));
    EXPECT_FALSE(nvr::store::verifyPassword("wrong",   h));
}
