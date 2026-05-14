#include "nvr/onvif/WsDiscovery.hpp"

#include "nvr/Logger.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <random>
#include <regex>
#include <set>

namespace nvr::onvif {

namespace {

constexpr const char* kProbeXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<e:Envelope xmlns:e="http://www.w3.org/2003/05/soap-envelope"
            xmlns:w="http://schemas.xmlsoap.org/ws/2004/08/addressing"
            xmlns:d="http://schemas.xmlsoap.org/ws/2005/04/discovery"
            xmlns:dn="http://www.onvif.org/ver10/network/wsdl">
  <e:Header>
    <w:MessageID>urn:uuid:{UUID}</w:MessageID>
    <w:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</w:To>
    <w:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</w:Action>
  </e:Header>
  <e:Body>
    <d:Probe>
      <d:Types>dn:NetworkVideoTransmitter</d:Types>
    </d:Probe>
  </e:Body>
</e:Envelope>)";

std::string genUuid() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    char buf[37];
    std::snprintf(buf, sizeof(buf), "%08lx-%04lx-%04lx-%04lx-%012lx",
                  rng() & 0xffffffff, rng() & 0xffff,
                  rng() & 0xffff,     rng() & 0xffff,
                  rng() & 0xffffffffffffULL);
    return buf;
}

std::string findTag(const std::string& xml, const std::string& tag) {
    auto open  = "<" + tag;
    auto close = "</" + tag + ">";
    auto a = xml.find(open);
    if (a == std::string::npos) return {};
    a = xml.find('>', a);
    if (a == std::string::npos) return {};
    ++a;
    auto b = xml.find(close, a);
    if (b == std::string::npos) return {};
    return xml.substr(a, b - a);
}

}

std::vector<Discovered> probe(std::chrono::milliseconds timeout) {
    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { NVR_WARN("onvif", "socket() failed"); return {}; }

    int ttl = 1;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in dst{};
    dst.sin_family      = AF_INET;
    dst.sin_port        = htons(3702);
    inet_pton(AF_INET, "239.255.255.250", &dst.sin_addr);

    std::string xml = kProbeXml;
    auto pos = xml.find("{UUID}");
    if (pos != std::string::npos) xml.replace(pos, 6, genUuid());

    if (::sendto(sock, xml.data(), xml.size(), 0,
                  reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) < 0) {
        NVR_WARN("onvif", "sendto failed: %s", std::strerror(errno));
        ::close(sock);
        return {};
    }

    timeval tv{};
    tv.tv_sec  = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::vector<Discovered> out;
    std::set<std::string> seen;
    char buf[8192];
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        sockaddr_in src{};
        socklen_t   src_len = sizeof(src);
        ssize_t n = ::recvfrom(sock, buf, sizeof(buf) - 1, 0,
                                reinterpret_cast<sockaddr*>(&src), &src_len);
        if (n <= 0) break;
        buf[n] = 0;
        std::string body(buf, static_cast<size_t>(n));

        Discovered d;
        d.xaddrs = findTag(body, "d:XAddrs");
        if (d.xaddrs.empty()) d.xaddrs = findTag(body, "wsdd:XAddrs");
        d.types  = findTag(body, "d:Types");
        d.scopes = findTag(body, "d:Scopes");

        char ip[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &src.sin_addr, ip, sizeof(ip));
        d.endpoint = ip;

        if (d.xaddrs.empty()) continue;
        if (!seen.insert(d.xaddrs).second) continue;
        out.push_back(std::move(d));
    }
    ::close(sock);
    NVR_INFO("onvif", "discovery: %zu device(s) found", out.size());
    return out;
}

}
