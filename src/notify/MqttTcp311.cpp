#include "nvr/notify/MqttTcp311.hpp"
#include "nvr/Logger.hpp"

#include <cerrno>
#include <netdb.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <random>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace nvr::notify {

namespace {

void appendUtf8(std::vector<uint8_t>& out, const std::string& s) {
    const uint16_t n = static_cast<uint16_t>(s.size());
    out.push_back(static_cast<uint8_t>((n >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(n & 0xff));
    for (unsigned char c : s) out.push_back(c);
}

void appendVarInt(std::vector<uint8_t>& out, uint32_t v) {
    do {
        uint8_t b = static_cast<uint8_t>(v % 128);
        v /= 128;
        if (v > 0) b |= 0x80;
        out.push_back(b);
    } while (v > 0);
}

bool setBlocking(int fd, bool block) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return false;
    if (block) fl &= ~O_NONBLOCK;
    else fl |= O_NONBLOCK;
    return fcntl(fd, F_SETFL, fl) == 0;
}

bool pollFd(int fd, bool want_read, int ms) {
    pollfd pfd{};
    pfd.fd     = fd;
    pfd.events = static_cast<short>(want_read ? POLLIN : POLLOUT);
    return ::poll(&pfd, 1, ms) > 0 && (pfd.revents & pfd.events);
}

bool readAll(int fd, uint8_t* buf, size_t n, int timeout_ms) {
    size_t got = 0;
    while (got < n) {
        if (!pollFd(fd, true, timeout_ms)) return false;
        ssize_t r = ::recv(fd, buf + got, n - got, 0);
        if (r <= 0) return false;
        got += static_cast<size_t>(r);
    }
    return true;
}

bool writeAll(int fd, const uint8_t* buf, size_t n, int timeout_ms) {
    size_t sent = 0;
    while (sent < n) {
        if (!pollFd(fd, false, timeout_ms)) return false;
        ssize_t w = ::send(fd, buf + sent, n - sent, MSG_NOSIGNAL);
        if (w <= 0) return false;
        sent += static_cast<size_t>(w);
    }
    return true;
}

} // namespace

bool mqttTcpPublishQos0(const std::string& broker_host, int broker_port, const std::string& username,
                         const std::string& password, const std::string& topic, const std::string& payload,
                         int timeout_sec) {
    if (broker_host.empty() || topic.empty()) return false;
    const int tmo_ms = std::max(1000, timeout_sec * 1000);

    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char              portbuf[16];
    std::snprintf(portbuf, sizeof(portbuf), "%d", broker_port);
    addrinfo* res = nullptr;
    if (getaddrinfo(broker_host.c_str(), portbuf, &hints, &res) != 0 || !res) {
        NVR_WARN("notify", "mqtt: resolve failed for %s", broker_host.c_str());
        return false;
    }

    int fd = -1;
    for (addrinfo* p = res; p; p = p->ai_next) {
        int tryfd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (tryfd < 0) continue;
        if (!setBlocking(tryfd, false)) {
            ::close(tryfd);
            continue;
        }
        if (::connect(tryfd, p->ai_addr, static_cast<socklen_t>(p->ai_addrlen)) != 0 &&
            errno != EINPROGRESS) {
            ::close(tryfd);
            continue;
        }
        if (!pollFd(tryfd, false, tmo_ms)) {
            ::close(tryfd);
            continue;
        }
        int       cerr  = 0;
        socklen_t elen = sizeof(cerr);
        if (::getsockopt(tryfd, SOL_SOCKET, SO_ERROR, &cerr, &elen) != 0 || cerr != 0) {
            ::close(tryfd);
            continue;
        }
        fd = tryfd;
        break;
    }
    freeaddrinfo(res);
    if (fd < 0) return false;

    std::random_device rd;
    std::mt19937       gen(rd());
    std::string        client_id = "nvr";
    for (int i = 0; i < 8; ++i) client_id.push_back("0123456789abcdef"[gen() & 15]);

    std::vector<uint8_t> variable;
    appendUtf8(variable, "MQTT");
    variable.push_back(4); // protocol level 3.1.1
    uint8_t flags = 0x02; // clean session
    if (!username.empty()) {
        flags |= 0x80;
        if (!password.empty()) flags |= 0x40;
    }
    variable.push_back(flags);
    variable.push_back(0);
    variable.push_back(60); // keepalive
    appendUtf8(variable, client_id);
    if (!username.empty()) {
        appendUtf8(variable, username);
        appendUtf8(variable, password);
    }

    std::vector<uint8_t> pkt;
    pkt.push_back(0x10); // CONNECT
    appendVarInt(pkt, static_cast<uint32_t>(variable.size()));
    pkt.insert(pkt.end(), variable.begin(), variable.end());

    if (!writeAll(fd, pkt.data(), pkt.size(), tmo_ms)) {
        ::close(fd);
        return false;
    }

    uint8_t connack[4]{};
    if (!readAll(fd, connack, sizeof(connack), tmo_ms)) {
        ::close(fd);
        return false;
    }
    if (connack[0] != 0x20 || connack[3] != 0) {
        NVR_WARN("notify", "mqtt CONNACK failed (code=%u)", static_cast<unsigned>(connack[3]));
        ::close(fd);
        return false;
    }

    std::vector<uint8_t> pubvar;
    appendUtf8(pubvar, topic);
    pubvar.insert(pubvar.end(), payload.begin(), payload.end());

    std::vector<uint8_t> pub;
    pub.push_back(0x30); // PUBLISH qos0
    appendVarInt(pub, static_cast<uint32_t>(pubvar.size()));
    pub.insert(pub.end(), pubvar.begin(), pubvar.end());

    if (!writeAll(fd, pub.data(), pub.size(), tmo_ms)) {
        ::close(fd);
        return false;
    }

    uint8_t disc[] = {0xE0, 0x00};
    (void)writeAll(fd, disc, sizeof(disc), std::min(500, tmo_ms));
    ::close(fd);
    return true;
}

} // namespace nvr::notify
