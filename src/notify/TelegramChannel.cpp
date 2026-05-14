#include "nvr/notify/Channels.hpp"

#include "nvr/Logger.hpp"

#include <curl/curl.h>

#include <sstream>

namespace nvr::notify {

namespace {

// Escape Telegram MarkdownV2 reserved characters so a malicious event payload
// can't break out of the code block (e.g. inject `*bold*` or, worse, a link).
std::string escapeMarkdownV2(std::string_view in) {
    static const std::string kSpecial = "_*[]()~`>#+-=|{}.!\\";
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        if (kSpecial.find(c) != std::string::npos) out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

class TelegramChannel : public INotificationChannel {
public:
    TelegramChannel(std::string token, std::string chat_id)
        : token_(std::move(token)), chat_id_(std::move(chat_id)) {}
    std::string kind() const override { return "telegram"; }

    bool send(const SystemEvent& ev) override {
        if (token_.empty() || chat_id_.empty()) return false;

        std::ostringstream txt;
        txt << "*\\[NVR\\]* `" << escapeMarkdownV2(ev.type) << "` "
            << escapeMarkdownV2(ev.severity)
            << "\nCamera: `" << escapeMarkdownV2(ev.camera_id) << "`"
            << "\n```\n" << ev.payload_json << "\n```";

        CURL* c = curl_easy_init();
        if (!c) return false;
        std::string url = "https://api.telegram.org/bot" + token_ + "/sendMessage";
        std::string post;
        {
            char* esc = curl_easy_escape(c, txt.str().c_str(),
                                          static_cast<int>(txt.str().size()));
            post = "chat_id=" + chat_id_ +
                   "&parse_mode=MarkdownV2" +
                   "&text=" + esc;
            curl_free(esc);
        }
        curl_easy_setopt(c, CURLOPT_URL, url.c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, post.c_str());
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);

        CURLcode rc = curl_easy_perform(c);
        curl_easy_cleanup(c);
        if (rc != CURLE_OK) {
            NVR_WARN("notify", "telegram failed: %s", curl_easy_strerror(rc));
            return false;
        }
        return true;
    }

private:
    std::string token_, chat_id_;
};

}

std::unique_ptr<INotificationChannel> makeTelegram(const nlohmann::json& cfg) {
    return std::make_unique<TelegramChannel>(
        cfg.value("bot_token", std::string{}),
        cfg.value("chat_id",   std::string{}));
}

}
