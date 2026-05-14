#include "nvr/notify/Channels.hpp"

#include "nvr/Logger.hpp"

#include <cstdio>
#include <sodium.h>

#include <curl/curl.h>

namespace nvr::notify {

namespace {

class WebhookChannel : public INotificationChannel {
public:
    WebhookChannel(std::string url, std::string secret_header, std::vector<unsigned char> hmac_key)
        : url_(std::move(url)),
          secret_header_(std::move(secret_header)),
          hmac_key_(std::move(hmac_key)) {}
    std::string kind() const override { return "webhook"; }

    bool send(const SystemEvent& ev) override {
        if (url_.empty()) return false;
        // Reject non-https/http URLs to avoid file://, gopher://, etc. via curl.
        if (url_.rfind("http://", 0) != 0 && url_.rfind("https://", 0) != 0) return false;

        nlohmann::json j = {
            {"camera_id",     ev.camera_id},
            {"type",          ev.type},
            {"severity",      ev.severity},
            {"snapshot_path", ev.snapshot_path},
            {"clip_path",     ev.clip_path},
            {"payload",       nlohmann::json::parse(ev.payload_json, nullptr, false)},
        };
        auto body = j.dump();

        CURL* c = curl_easy_init();
        if (!c) return false;

        struct curl_slist* hdrs = nullptr;
        hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
        if (!secret_header_.empty()) hdrs = curl_slist_append(hdrs, secret_header_.c_str());
        if (!hmac_key_.empty()) {
            unsigned char mac[crypto_auth_hmacsha256_BYTES];
            crypto_auth_hmacsha256(mac, reinterpret_cast<const unsigned char*>(body.data()),
                                    body.size(), hmac_key_.data());
            char hex[crypto_auth_hmacsha256_BYTES * 2 + 1];
            for (size_t i = 0; i < sizeof(mac); ++i)
                std::sprintf(hex + i * 2, "%02x", static_cast<unsigned>(mac[i]));
            std::string sig_hdr = std::string("X-NVR-Signature: sha256=") + hex;
            hdrs                  = curl_slist_append(hdrs, sig_hdr.c_str());
        }

        curl_easy_setopt(c, CURLOPT_URL, url_.c_str());
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https,http");
        curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https,http");

        CURLcode rc = curl_easy_perform(c);
        long http_code = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
        curl_slist_free_all(hdrs);
        curl_easy_cleanup(c);
        if (rc != CURLE_OK) {
            NVR_WARN("notify", "webhook failed: %s", curl_easy_strerror(rc));
            return false;
        }
        if (http_code >= 400) {
            NVR_WARN("notify", "webhook %s returned %ld", url_.c_str(), http_code);
            return false;
        }
        return true;
    }

private:
    std::string                url_;
    std::string                secret_header_;
    std::vector<unsigned char> hmac_key_;
};

}

std::unique_ptr<INotificationChannel> makeWebhook(const nlohmann::json& cfg) {
    auto sanitize = [](std::string s) {
        for (auto& c : s) if (c == '\r' || c == '\n' || c == '\0') c = ' ';
        return s;
    };
    std::string sh;
    if (cfg.contains("secret_header_name") && cfg.contains("secret_header_value")) {
        auto name  = sanitize(cfg["secret_header_name"].get<std::string>());
        auto value = sanitize(cfg["secret_header_value"].get<std::string>());
        if (!name.empty() && !value.empty()) sh = name + ": " + value;
    }
    std::vector<unsigned char> hmac_key;
    if (cfg.contains("webhook_hmac_sha256_key_base64")) {
        const auto b64 = cfg["webhook_hmac_sha256_key_base64"].get<std::string>();
        hmac_key.resize(256);
        size_t binlen = 0;
        const char* skip = nullptr;
        if (sodium_base642bin(hmac_key.data(), hmac_key.size(), b64.c_str(), b64.size(), &skip,
                              &binlen, nullptr, sodium_base64_VARIANT_ORIGINAL) == 0 &&
            binlen > 0) {
            hmac_key.resize(binlen);
        } else {
            hmac_key.clear();
        }
    }
    return std::make_unique<WebhookChannel>(cfg.value("url", std::string{}), sh, std::move(hmac_key));
}

}
