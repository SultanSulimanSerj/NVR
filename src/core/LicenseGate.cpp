#include "nvr/LicenseGate.hpp"

#include "nvr/Logger.hpp"
#include "nvr/license_canonical.hpp"

#include <nlohmann/json.hpp>

#include <sodium.h>

#include <chrono>
#include <ctime>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace nvr {

namespace {

std::string trimWs(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

bool readTextFile(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    if (sz < 0 || sz > 1024 * 1024) return false;
    f.seekg(0);
    out.resize(static_cast<size_t>(sz));
    if (sz > 0) f.read(out.data(), sz);
    return static_cast<bool>(f);
}

bool readBinaryFile(const fs::path& p, std::vector<unsigned char>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    if (sz < 0 || sz > 4096) return false;
    f.seekg(0);
    out.resize(static_cast<size_t>(sz));
    if (sz > 0) f.read(reinterpret_cast<char*>(out.data()), sz);
    return static_cast<bool>(f);
}

std::string toHex(const unsigned char* p, size_t n) {
    static const char* h = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s += h[p[i] >> 4];
        s += h[p[i] & 15];
    }
    return s;
}

std::string fpFromBasis(const std::string& basis) {
    unsigned char out[crypto_generichash_BYTES];
    if (crypto_generichash(out, sizeof(out), reinterpret_cast<const unsigned char*>(basis.data()),
                           basis.size(), nullptr, 0) != 0) {
        return {};
    }
    return toHex(out, sizeof(out));
}

std::string firstMacLower() {
#ifdef __linux__
    std::error_code ec;
    for (const auto& ent : fs::directory_iterator("/sys/class/net", ec)) {
        if (ec) break;
        const auto name = ent.path().filename().string();
        if (name == "lo") continue;
        std::ifstream a(ent.path() / "address");
        std::string line;
        if (!std::getline(a, line)) continue;
        line = trimWs(line);
        for (auto& c : line) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!line.empty()) return line;
    }
#endif
    return "nomac";
}

std::vector<unsigned char> decodeBase64(const std::string& in) {
    std::string cleaned;
    cleaned.reserve(in.size());
    for (char c : in) {
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        cleaned.push_back(c);
    }
    if (cleaned.empty()) return {};
    std::vector<unsigned char> out(cleaned.size());
    size_t      binlen = 0;
    const char* skip   = nullptr;
    if (sodium_base642bin(out.data(), out.size(), cleaned.data(), cleaned.size(), &skip, &binlen,
                          nullptr, sodium_base64_VARIANT_ORIGINAL) != 0) {
        return {};
    }
    out.resize(binlen);
    return out;
}

} // namespace

std::string LicenseGate::computeMachineFingerprint() {
#ifdef __linux__
    std::string mid;
    {
        std::ifstream f("/etc/machine-id");
        if (f) std::getline(f, mid);
        mid = trimWs(mid);
    }
    if (mid.empty()) mid = "nomachineid";
    const std::string basis = mid + "|" + firstMacLower();
    return fpFromBasis(basis);
#else
    return fpFromBasis("nonlinux-dev");
#endif
}

LicenseGate::LicenseGate(const LicensePaths& paths, std::string fingerprint_override)
    : paths_(paths) {
    if (sodium_init() < 0) throw std::runtime_error("libsodium init failed");

    if (!fingerprint_override.empty()) fp_hex_ = std::move(fingerprint_override);
    else fp_hex_ = computeMachineFingerprint();

    int trial = paths_.trial_max_channels;
    if (trial < 1) trial = 8;
    if (trial > 1024) trial = 1024;
    paths_.trial_max_channels = trial;

    evaluateSignedLicense_();
}

void LicenseGate::resetToTrialChannelLimits_() {
    signature_ok_ = false;
    max_channels_ = paths_.trial_max_channels;
    modules_      = 0;
    exp_          = 0;
}

void LicenseGate::markTrial_(const char* log_reason) {
    state_ = State::Trial;
    resetToTrialChannelLimits_();
    if (log_reason && *log_reason) NVR_WARN("license", "%s", log_reason);
}

void LicenseGate::markDegraded_(const char* log_reason) {
    state_ = State::Degraded;
    resetToTrialChannelLimits_();
    if (log_reason && *log_reason) NVR_WARN("license", "%s", log_reason);
}

void LicenseGate::evaluateSignedLicense_() {
    const fs::path lic_path(paths_.file);
    const fs::path pub_path(paths_.public_key);

    license_file_present_ = fs::exists(lic_path) && fs::file_size(lic_path) > 0;

    std::vector<unsigned char> pubkey;
    if (!readBinaryFile(pub_path, pubkey) || pubkey.size() != crypto_sign_PUBLICKEYBYTES) {
        if (license_file_present_) {
            NVR_WARN("license", "license file present but public key missing or invalid; using trial");
        }
        markTrial_("trial mode (no valid license.pub)");
        return;
    }

    if (!license_file_present_) {
        markTrial_("trial mode (no license file)");
        return;
    }

    std::string lic_raw;
    if (!readTextFile(lic_path, lic_raw)) {
        attempted_verify_ = true;
        markDegraded_("could not read license file; degraded trial");
        return;
    }

    json root;
    try {
        root = json::parse(lic_raw);
    } catch (...) {
        attempted_verify_ = true;
        markDegraded_("invalid license JSON; degraded trial");
        return;
    }

    if (!root.contains("payload") || !root["payload"].is_object() || !root.contains("sig") ||
        !root["sig"].is_string()) {
        attempted_verify_ = true;
        markDegraded_("license.lic missing payload/sig; degraded trial");
        return;
    }

    attempted_verify_ = true;
    const json& pl    = root["payload"];
    std::string fp;
    int64_t     expv = 0;
    int         max_ch = 0;
    uint32_t    modv = 0;
    try {
        fp     = pl.at("fp").get<std::string>();
        expv   = pl.at("exp").get<int64_t>();
        max_ch = pl.at("max_ch").get<int>();
        if (pl.at("mod").is_number_unsigned()) modv = pl.at("mod").get<uint32_t>();
        else modv = static_cast<uint32_t>(pl.at("mod").get<int64_t>());
    } catch (...) {
        markDegraded_("license payload fields invalid; degraded trial");
        return;
    }

    const std::string msg = license_detail::canonicalLicensePayloadString(fp, expv, max_ch, modv);
    const std::string sigb64 = root["sig"].get<std::string>();
    const auto        sigbin = decodeBase64(sigb64);
    if (sigbin.size() != crypto_sign_BYTES) {
        markDegraded_("bad signature base64; degraded trial");
        return;
    }

    if (crypto_sign_verify_detached(sigbin.data(),
                                     reinterpret_cast<const unsigned char*>(msg.data()), msg.size(),
                                     pubkey.data()) != 0) {
        markDegraded_("Ed25519 verify failed; degraded trial");
        return;
    }

    if (fp != fp_hex_) {
        markDegraded_("license fingerprint mismatch; degraded trial");
        return;
    }

    if (expv > 0) {
        const auto now = std::chrono::system_clock::now();
        const auto exp =
            std::chrono::system_clock::from_time_t(static_cast<std::time_t>(expv));
        if (now > exp) {
            markDegraded_("license expired; degraded trial");
            return;
        }
    }

    if (max_ch < 1) max_ch = 1;
    if (max_ch > 8192) max_ch = 8192;

    state_         = State::Licensed;
    signature_ok_  = true;
    max_channels_  = max_ch;
    modules_       = modv;
    exp_           = expv;
    NVR_INFO("license", "licensed: max_ch=%d mod=%u", max_channels_, static_cast<unsigned>(modules_));
}

json LicenseGate::statusJson(std::size_t cameras_configured) const {
    const char* mode = "trial";
    if (state_ == State::Licensed) mode = "licensed";
    else if (state_ == State::Degraded) mode = "degraded";

    json j;
    j["mode"]                  = mode;
    j["max_channels"]          = max_channels_;
    j["channels_configured"]   = cameras_configured;
    j["modules_bitmask"]       = modules_;
    j["signature_ok"]          = signature_ok_;
    j["license_file_present"]  = license_file_present_;
    j["attempted_verify"]      = attempted_verify_;
    j["fingerprint_preview"]     = fp_hex_.size() >= 8 ? fp_hex_.substr(0, 8) : fp_hex_;
    if (exp_ == 0) j["expires_at"] = nullptr;
    else {
        std::time_t t = static_cast<std::time_t>(exp_);
        std::tm     g{};
#if defined(__linux__)
        gmtime_r(&t, &g);
#else
        if (std::tm* p = std::gmtime(&t)) g = *p;
#endif
        char buf[32];
        if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &g) > 0) j["expires_at"] = buf;
        else j["expires_at"] = static_cast<int64_t>(exp_);
    }
    return j;
}

bool LicenseGate::allowAiModule(unsigned bit_index) const noexcept {
    if (bit_index >= 32) return false;
    if (state_ != State::Licensed || !signature_ok_) return false;
    return (modules_ & (1u << bit_index)) != 0;
}

} // namespace nvr
