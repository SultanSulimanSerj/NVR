#include "nvr/notify/Channels.hpp"

#include "nvr/Logger.hpp"

#include <curl/curl.h>

#include <cstring>
#include <sstream>

namespace nvr::notify {

namespace {

// Replace any CR/LF / control characters with a space so an attacker can never
// inject extra SMTP headers via the event payload (header injection / SMTP smuggling).
std::string sanitizeHeader(std::string s, size_t max_len = 200) {
    if (s.size() > max_len) s.resize(max_len);
    for (auto& c : s) {
        if (c == '\r' || c == '\n' || c == '\t' || c == '\0' ||
            static_cast<unsigned char>(c) < 0x20) c = ' ';
    }
    return s;
}

struct EmailConfig {
    std::string smtp_url;
    std::string user;
    std::string password;
    std::string from;
    std::vector<std::string> to;
    bool        use_starttls{true};
};

class EmailChannel : public INotificationChannel {
public:
    explicit EmailChannel(EmailConfig c) : cfg_(std::move(c)) {}
    std::string kind() const override { return "email"; }

    bool send(const SystemEvent& ev) override {
        if (cfg_.smtp_url.empty() || cfg_.from.empty() || cfg_.to.empty()) return false;

        const std::string sub_cam  = sanitizeHeader(ev.camera_id, 64);
        const std::string sub_type = sanitizeHeader(ev.type,      64);
        const std::string sub_sev  = sanitizeHeader(ev.severity,  32);
        const std::string from_san = sanitizeHeader(cfg_.from,    254);

        std::ostringstream payload;
        payload << "From: " << from_san << "\r\n"
                << "Subject: [NVR] " << sub_type << " on " << sub_cam << "\r\n"
                << "MIME-Version: 1.0\r\n"
                << "Content-Type: text/plain; charset=utf-8\r\n\r\n"
                << "Camera: " << sub_cam << "\r\n"
                << "Type:   " << sub_type << "\r\n"
                << "Sev.:   " << sub_sev << "\r\n"
                << "Payload: " << ev.payload_json << "\r\n";

        const std::string body = payload.str();
        size_t offset = 0;

        CURL* c = curl_easy_init();
        if (!c) return false;

        curl_easy_setopt(c, CURLOPT_URL, cfg_.smtp_url.c_str());
        if (!cfg_.user.empty()) curl_easy_setopt(c, CURLOPT_USERNAME, cfg_.user.c_str());
        if (!cfg_.password.empty()) curl_easy_setopt(c, CURLOPT_PASSWORD, cfg_.password.c_str());
        if (cfg_.use_starttls) curl_easy_setopt(c, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        curl_easy_setopt(c, CURLOPT_MAIL_FROM, cfg_.from.c_str());

        struct curl_slist* recipients = nullptr;
        for (auto& r : cfg_.to) recipients = curl_slist_append(recipients, r.c_str());
        curl_easy_setopt(c, CURLOPT_MAIL_RCPT, recipients);
        curl_easy_setopt(c, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(c, CURLOPT_READFUNCTION,
            +[](char* ptr, size_t size, size_t nmemb, void* userp) -> size_t {
                auto* o = static_cast<std::pair<const std::string*, size_t>*>(userp);
                size_t left = o->first->size() - o->second;
                size_t n = std::min(size * nmemb, left);
                if (n == 0) return 0;
                std::memcpy(ptr, o->first->data() + o->second, n);
                o->second += n;
                return n;
            });
        auto stream = std::make_pair(&body, offset);
        curl_easy_setopt(c, CURLOPT_READDATA, &stream);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);

        CURLcode rc = curl_easy_perform(c);
        curl_slist_free_all(recipients);
        curl_easy_cleanup(c);
        if (rc != CURLE_OK) {
            NVR_WARN("notify", "email send failed: %s", curl_easy_strerror(rc));
            return false;
        }
        return true;
    }

private:
    EmailConfig cfg_;
};

}

std::unique_ptr<INotificationChannel> makeEmail(const nlohmann::json& cfg) {
    EmailConfig c;
    c.smtp_url     = cfg.value("smtp_url",     std::string{});
    c.user         = cfg.value("user",         std::string{});
    c.password     = cfg.value("password",     std::string{});
    c.from         = cfg.value("from",         std::string{});
    c.use_starttls = cfg.value("use_starttls", true);
    for (auto& v : cfg.value("to", nlohmann::json::array())) c.to.push_back(v.get<std::string>());
    return std::make_unique<EmailChannel>(std::move(c));
}

}
