#pragma once

#include "nvr/Config.hpp" // LicensePaths

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace nvr {

/// Offline license verification + trial fallback (see docs/LICENSE_TICKET.md).
class LicenseGate {
public:
    explicit LicenseGate(const LicensePaths& paths, std::string fingerprint_override = {});

    static std::string computeMachineFingerprint();

    int      effectiveMaxChannels() const noexcept { return max_channels_; }
    uint32_t modulesBitmask() const noexcept { return modules_; }
    int64_t  expiresUnix() const noexcept { return exp_; }

    enum class State { Trial, Licensed, Degraded };
    State state() const noexcept { return state_; }

    bool signatureValid() const noexcept { return signature_ok_; }
    bool attemptedFileVerify() const noexcept { return attempted_verify_; }

    nlohmann::json statusJson(std::size_t cameras_configured) const;

    /// Licensed + valid signature and module bit `1<<bit_index` (see docs/LICENSE_TICKET.md).
    bool allowAiModule(unsigned bit_index) const noexcept;

private:
    void resetToTrialChannelLimits_();
    void markTrial_(const char* log_reason);
    void markDegraded_(const char* log_reason);
    void evaluateSignedLicense_();

    LicensePaths paths_;
    std::string  fp_hex_;
    int          max_channels_{8};
    uint32_t     modules_{0};
    int64_t      exp_{0};
    State        state_{State::Trial};
    bool         signature_ok_{false};
    bool         attempted_verify_{false};
    bool         license_file_present_{false};
};

} // namespace nvr
