#include "nvr/notify/Channels.hpp"

#include "nvr/Logger.hpp"

#include <chrono>
#include <cstring>
#include <ctime>
#include <netdb.h>
#include <sstream>
#include <unistd.h>

#include <sys/socket.h>

#include <nlohmann/json.hpp>

namespace nvr::notify {

namespace {

class SyslogChannel : public INotificationChannel {
public:
    SyslogChannel(std::string host, int port) : host_(std::move(host)), port_(port) {}

    std::string kind() const override { return "syslog"; }

    bool send(const SystemEvent& ev) override {
        if (host_.empty() || port_ <= 0 || port_ > 65535) return false;

        nlohmann::json j = {
            {"camera_id",     ev.camera_id},
            {"type",          ev.type},
            {"severity",      ev.severity},
            {"snapshot_path", ev.snapshot_path},
            {"clip_path",     ev.clip_path},
            {"payload",       nlohmann::json::parse(ev.payload_json, nullptr, false)},
        };
        std::string msg = j.dump();
        for (auto& c : msg) {
            if (c == '\n' || c == '\r') c = ' ';
        }

        addrinfo hints{};
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* res = nullptr;
        char      portbuf[16];
        std::snprintf(portbuf, sizeof(portbuf), "%d", port_);
        if (getaddrinfo(host_.c_str(), portbuf, &hints, &res) != 0 || !res) {
            NVR_WARN("notify", "syslog: resolve failed for %s", host_.c_str());
            return false;
        }

        int fd = -1;
        for (addrinfo* p = res; p; p = p->ai_next) {
            fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (fd < 0) continue;
            if (::connect(fd, p->ai_addr, static_cast<socklen_t>(p->ai_addrlen)) == 0) break;
            ::close(fd);
            fd = -1;
        }
        freeaddrinfo(res);
        if (fd < 0) {
            NVR_WARN("notify", "syslog: connect udp failed %s:%d", host_.c_str(), port_);
            return false;
        }

        char hostname[256]{};
        if (gethostname(hostname, sizeof(hostname) - 1) != 0) std::strncpy(hostname, "nvr", sizeof(hostname) - 1);

        auto     tp = std::chrono::system_clock::to_time_t(ev.ts);
        std::tm  tm{};
        gmtime_r(&tp, &tm);
        char tsbuf[40];
        std::strftime(tsbuf, sizeof(tsbuf), "%Y-%m-%dT%H:%M:%SZ", &tm);

        // RFC5424: <PRI>1 TIMESTAMP HOST APP PROCID MSGID STRUCTURED-DATA MSG
        const int     pri = 16 * 8 + 6; // local0 + informational
        std::ostringstream line;
        line << '<' << pri << ">1 " << tsbuf << ' ' << hostname << " nvr-prototype - - - " << msg;

        const std::string& s = line.str();
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
        ssize_t n = ::send(fd, s.data(), s.size(), MSG_NOSIGNAL);
        ::close(fd);
        if (n != static_cast<ssize_t>(s.size())) {
            NVR_WARN("notify", "syslog: short send");
            return false;
        }
        return true;
    }

private:
    std::string host_;
    int         port_{};
};

} // namespace

std::unique_ptr<INotificationChannel> makeSyslog(const nlohmann::json& cfg) {
    const std::string host = cfg.value("syslog_host", std::string("127.0.0.1"));
    const int         port = cfg.value("syslog_port", 514);
    return std::make_unique<SyslogChannel>(host, port);
}

} // namespace nvr::notify
