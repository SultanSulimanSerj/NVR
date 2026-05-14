#include "nvr/LicenseGate.hpp"
#include "nvr/license_canonical.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sodium.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

TEST(LicenseGate, TrialWhenMissingFiles) {
    nvr::LicensePaths p;
    p.file         = "/nonexistent/nvr_license.lic";
    p.public_key   = "/nonexistent/nvr_license.pub";
    p.trial_max_channels = 8;
    const std::string fp(64, 'a');
    nvr::LicenseGate g(p, fp);
    EXPECT_EQ(g.state(), nvr::LicenseGate::State::Trial);
    EXPECT_EQ(g.effectiveMaxChannels(), 8);
    EXPECT_FALSE(g.signatureValid());
}

TEST(LicenseGate, LicensedWhenSignatureOk) {
    ASSERT_EQ(sodium_init(), 0);
    unsigned char pk[crypto_sign_PUBLICKEYBYTES];
    unsigned char sk[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(pk, sk);

    const auto dir = fs::temp_directory_path() / "nvr_license_gate_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto pubp = dir / "license.pub";
    const auto licp = dir / "license.lic";
    {
        std::ofstream o(pubp, std::ios::binary);
        o.write(reinterpret_cast<const char*>(pk), static_cast<std::streamsize>(sizeof(pk)));
    }

    const std::string fp(64, 'b');
    const int64_t     exp   = 0;
    const int         max_ch = 16;
    const uint32_t    mod    = 0;
    const std::string canon  = nvr::license_detail::canonicalLicensePayloadString(fp, exp, max_ch, mod);
    unsigned char     sig[crypto_sign_BYTES];
    ASSERT_EQ(0, crypto_sign_detached(sig, nullptr,
                                      reinterpret_cast<const unsigned char*>(canon.data()), canon.size(),
                                      sk));

    char b64[sodium_base64_encoded_len(sizeof(sig), sodium_base64_VARIANT_ORIGINAL) + 1U];
    sodium_bin2base64(b64, sizeof(b64), sig, sizeof(sig), sodium_base64_VARIANT_ORIGINAL);

    json root;
    root["payload"] = json{{"fp", fp}, {"max_ch", max_ch}, {"mod", mod}, {"exp", exp}};
    root["sig"]     = std::string(b64);
    {
        std::ofstream o(licp, std::ios::binary);
        o << root.dump();
    }

    nvr::LicensePaths p;
    p.file               = licp.string();
    p.public_key         = pubp.string();
    p.trial_max_channels = 3;

    nvr::LicenseGate g(p, fp);
    EXPECT_EQ(g.state(), nvr::LicenseGate::State::Licensed);
    EXPECT_EQ(g.effectiveMaxChannels(), 16);
    EXPECT_TRUE(g.signatureValid());
    EXPECT_FALSE(g.allowAiModule(0));

    fs::remove_all(dir);
}

TEST(LicenseGate, DegradedWhenFingerprintMismatch) {
    ASSERT_EQ(sodium_init(), 0);
    unsigned char pk[crypto_sign_PUBLICKEYBYTES];
    unsigned char sk[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(pk, sk);

    const auto dir = fs::temp_directory_path() / "nvr_license_gate_test2";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto pubp = dir / "license.pub";
    const auto licp = dir / "license.lic";
    {
        std::ofstream o(pubp, std::ios::binary);
        o.write(reinterpret_cast<const char*>(pk), static_cast<std::streamsize>(sizeof(pk)));
    }

    const std::string fp_payload(64, 'c');
    const std::string fp_machine(64, 'd');
    const int64_t     exp    = 0;
    const int         max_ch = 4;
    const uint32_t    mod    = 0;
    const std::string canon =
        nvr::license_detail::canonicalLicensePayloadString(fp_payload, exp, max_ch, mod);
    unsigned char sig[crypto_sign_BYTES];
    ASSERT_EQ(0, crypto_sign_detached(sig, nullptr,
                                      reinterpret_cast<const unsigned char*>(canon.data()), canon.size(),
                                      sk));
    char b64[sodium_base64_encoded_len(sizeof(sig), sodium_base64_VARIANT_ORIGINAL) + 1U];
    sodium_bin2base64(b64, sizeof(b64), sig, sizeof(sig), sodium_base64_VARIANT_ORIGINAL);
    json root;
    root["payload"] = json{{"fp", fp_payload}, {"max_ch", max_ch}, {"mod", mod}, {"exp", exp}};
    root["sig"]     = std::string(b64);
    {
        std::ofstream o(licp, std::ios::binary);
        o << root.dump();
    }

    nvr::LicensePaths p;
    p.file               = licp.string();
    p.public_key         = pubp.string();
    p.trial_max_channels = 7;

    nvr::LicenseGate g(p, fp_machine);
    EXPECT_EQ(g.state(), nvr::LicenseGate::State::Degraded);
    EXPECT_EQ(g.effectiveMaxChannels(), 7);
    EXPECT_FALSE(g.signatureValid());

    fs::remove_all(dir);
}

TEST(LicenseGate, AllowAiModuleRequiresLicensedSignatureAndBit) {
    ASSERT_EQ(sodium_init(), 0);
    unsigned char pk[crypto_sign_PUBLICKEYBYTES];
    unsigned char sk[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(pk, sk);

    const auto dir = fs::temp_directory_path() / "nvr_license_ai_mod";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto pubp = dir / "license.pub";
    const auto licp = dir / "license.lic";
    {
        std::ofstream o(pubp, std::ios::binary);
        o.write(reinterpret_cast<const char*>(pk), static_cast<std::streamsize>(sizeof(pk)));
    }

    const std::string fp(64, 'c');
    const int64_t     exp    = 0;
    const int         max_ch = 8;
    const uint32_t    mod    = 1u;
    const std::string canon  = nvr::license_detail::canonicalLicensePayloadString(fp, exp, max_ch, mod);
    unsigned char     sig[crypto_sign_BYTES];
    ASSERT_EQ(0, crypto_sign_detached(sig, nullptr,
                                      reinterpret_cast<const unsigned char*>(canon.data()), canon.size(),
                                      sk));
    char b64[sodium_base64_encoded_len(sizeof(sig), sodium_base64_VARIANT_ORIGINAL) + 1U];
    sodium_bin2base64(b64, sizeof(b64), sig, sizeof(sig), sodium_base64_VARIANT_ORIGINAL);
    json root;
    root["payload"] = json{{"fp", fp}, {"max_ch", max_ch}, {"mod", mod}, {"exp", exp}};
    root["sig"]     = std::string(b64);
    {
        std::ofstream o(licp, std::ios::binary);
        o << root.dump();
    }

    nvr::LicensePaths p;
    p.file               = licp.string();
    p.public_key         = pubp.string();
    p.trial_max_channels = 3;

    nvr::LicenseGate g(p, fp);
    EXPECT_TRUE(g.allowAiModule(0));
    EXPECT_FALSE(g.allowAiModule(1));
    EXPECT_FALSE(g.allowAiModule(40));

    fs::remove_all(dir);
}
