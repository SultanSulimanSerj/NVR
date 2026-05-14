#include "nvr/onvif/EnumerateStreams.hpp"

#include "nvr/onvif/SoapClient.hpp"
#include "nvr/Logger.hpp"

#include <algorithm>
#include <regex>
#include <sstream>

namespace nvr::onvif {

namespace {

std::optional<std::string> extractMediaXAddr(const std::string& cap) {
    static const std::regex mediaBlock(
        R"(<(?:tt:)?Media(?:\s[^>]*)?>([\s\S]*?)</(?:tt:)?Media>)", std::regex::icase);
    std::smatch bm;
    if (!std::regex_search(cap, bm, mediaBlock)) return std::nullopt;
    const std::string inner = bm[1].str();
    static const std::regex xaddr(
        R"(<(?:tt:)?XAddr>\s*([^<\s]+)\s*</(?:tt:)?XAddr>)", std::regex::icase);
    std::smatch xm;
    if (!std::regex_search(inner, xm, xaddr)) return std::nullopt;
    return xm[1].str();
}

std::string guessMediaServiceUrl(const std::string& device_service_url) {
    const std::string from = "device_service";
    auto                   p = device_service_url.find(from);
    if (p != std::string::npos) {
        std::string u = device_service_url;
        u.replace(p, from.size(), "media_service");
        return u;
    }
    const auto scheme = device_service_url.find("://");
    if (scheme == std::string::npos) return device_service_url + "/onvif/media_service";
    const auto start  = scheme + 3;
    const auto path   = device_service_url.find('/', start);
    const std::string origin =
        path == std::string::npos ? device_service_url : device_service_url.substr(0, path);
    return origin + "/onvif/media_service";
}

bool parseHttpAuthority(const std::string& url, std::string& host_out, int& port_out) {
    const auto scheme = url.find("://");
    if (scheme == std::string::npos) return false;
    const size_t start = scheme + 3;
    const size_t path  = url.find('/', start);
    const std::string authority =
        path == std::string::npos ? url.substr(start) : url.substr(start, path - start);
    const size_t colon = authority.find(':');
    if (colon != std::string::npos) {
        host_out = authority.substr(0, colon);
        try {
            port_out = std::stoi(authority.substr(colon + 1));
        } catch (...) {
            return false;
        }
    } else {
        host_out   = authority;
        port_out   = (url.size() >= 8 && url.substr(0, 8) == "https://") ? 443 : 80;
    }
    return !host_out.empty();
}

std::string xmlEscapeProfileToken(const std::string& t) {
    std::string o;
    o.reserve(t.size() + 8);
    for (unsigned char c : t) {
        if (c == '&')
            o += "&amp;";
        else if (c == '<')
            o += "&lt;";
        else if (c == '>')
            o += "&gt;";
        else if (c == '"')
            o += "&quot;";
        else
            o += static_cast<char>(c);
    }
    return o;
}

} // namespace

EnumerateStreamsResult enumerateStreams(const std::string& device_service_url,
                                        const std::string& user,
                                        const std::string& pass) {
    EnumerateStreamsResult out;
    out.device_service_url = device_service_url;
    if (!parseHttpAuthority(device_service_url, out.onvif_host, out.onvif_port)) {
        out.error = "bad_device_url";
        return out;
    }

    SoapClient sc(device_service_url, user, pass);

    out.device = sc.getDeviceInformation();

    std::string media_url;
    if (auto cap = sc.getCapabilitiesXml()) {
        if (auto mx = extractMediaXAddr(*cap); mx && !mx->empty()) media_url = *mx;
    }
    if (media_url.empty()) media_url = guessMediaServiceUrl(device_service_url);
    out.media_service_url = media_url;

    auto profiles = sc.getProfiles(media_url);
    if (profiles.empty()) {
        out.error = "no_profiles_or_media_denied";
        NVR_WARN("onvif", "enumerateStreams: no profiles for %s", media_url.c_str());
        return out;
    }

    for (auto& p : profiles) {
        if (p.token.empty()) continue;
        const std::string tokEsc = xmlEscapeProfileToken(p.token);
        auto              uri    = sc.getStreamUri(media_url, tokEsc);
        if (!uri || uri->empty()) continue;
        EnumeratedStream row;
        row.profile_token = p.token;
        row.profile_name  = p.name;
        row.width         = p.width;
        row.height        = p.height;
        row.codec         = p.codec;
        row.uri           = *uri;
        out.streams.push_back(std::move(row));
    }

    std::sort(out.streams.begin(), out.streams.end(), [](const EnumeratedStream& a, const EnumeratedStream& b) {
        const long pa = static_cast<long>(a.width) * a.height;
        const long pb = static_cast<long>(b.width) * b.height;
        if (pa != pb) return pa > pb;
        return a.profile_name < b.profile_name;
    });

    if (out.streams.empty()) {
        out.error = "get_stream_uri_failed";
        return out;
    }

    out.ok = true;
    return out;
}

} // namespace nvr::onvif
