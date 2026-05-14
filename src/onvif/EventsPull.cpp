#include "nvr/onvif/EventsPull.hpp"

#include "nvr/onvif/SoapClient.hpp"
#include "nvr/Logger.hpp"

#include <regex>
#include <thread>

namespace nvr::onvif {

namespace {

std::optional<std::string> extractSubscriptionAddress(const std::string& xml) {
    static const std::regex rx(
        R"(<(?:[A-Za-z0-9_]+:)?Address>\s*([^<\s]+)\s*</(?:[A-Za-z0-9_]+:)?Address>)");
    std::smatch m;
    if (std::regex_search(xml, m, rx)) return m[1].str();
    return std::nullopt;
}

std::string absolutePullUrl(const std::string& events_service_url, const std::string& addr) {
    if (addr.rfind("http://", 0) == 0 || addr.rfind("https://", 0) == 0) return addr;
    // Relative path — attach to host:port of events_service_url
    auto p = events_service_url.find("://");
    if (p == std::string::npos) return addr;
    auto rest = events_service_url.substr(p + 3);
    auto slash = rest.find('/');
    if (slash == std::string::npos) return events_service_url + (addr.empty() || addr[0] == '/' ? "" : "/") + addr;
    const std::string origin = events_service_url.substr(0, p + 3 + slash);
    if (!addr.empty() && addr[0] == '/') return origin + addr;
    return origin + "/" + addr;
}

void appendMessagesFromPullResponse(const std::string& xml, std::vector<PulledOnvifEvent>& out) {
    static const std::regex msg(
        R"(<(?:wsnt:|tev:)?NotificationMessage[^>]*>([\s\S]*?)</(?:wsnt:|tev:)?NotificationMessage>)",
        std::regex::icase);
    std::sregex_iterator it(xml.begin(), xml.end(), msg), end;
    for (; it != end; ++it) {
        std::string chunk = it->str();
        std::string topic;
        static const std::regex topicRx(R"(<(?:[^>]+:)?Topic[^>]*>([^<]+)</(?:[^>]+:)?Topic>)",
                                         std::regex::icase);
        std::smatch tm;
        if (std::regex_search(chunk, tm, topicRx)) topic = tm[1].str();
        if (topic.empty()) topic = "onvif.unknown";
        if (chunk.size() > 8000) chunk.resize(8000);
        out.push_back({std::move(topic), std::move(chunk)});
    }
}

} // namespace

std::string resolveEventsServiceUrl(const std::string& host, int port, const std::string& user,
                                     const std::string& pass,
                                     const std::optional<std::string>& events_url_override) {
    if (events_url_override && !events_url_override->empty()) return *events_url_override;

    const std::string dev = "http://" + host + ":" + std::to_string(port) + "/onvif/device_service";
    SoapClient        sc(dev, user, pass);
    auto              cap = sc.getCapabilitiesXml();
    if (cap) {
        static const std::regex rx(
            R"(<tt:Events[^>]*>\s*<tt:XAddr>\s*([^<\s]+)\s*</tt:XAddr>)", std::regex::icase);
        std::smatch m;
        if (std::regex_search(*cap, m, rx)) return m[1].str();
    }
    return "http://" + host + ":" + std::to_string(port) + "/onvif/event_service";
}

std::vector<PulledOnvifEvent> pullPointOnce(const std::string& events_service_url,
                                            const std::string& user, const std::string& pass) {
    std::vector<PulledOnvifEvent> out;
    SoapClient                   base(events_service_url, user, pass);

    const char* kActSub = "http://www.onvif.org/ver10/events/wsdl/CreatePullPointSubscription";
    const std::string bodySub =
        "<tev:CreatePullPointSubscription "
        "xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\">"
        "<tev:InitialTerminationTime>PT60M</tev:InitialTerminationTime>"
        "</tev:CreatePullPointSubscription>";

    std::optional<std::string> subResp;
    for (int attempt = 0; attempt < 3; ++attempt) {
        subResp = base.callEvents(events_service_url, kActSub, bodySub);
        if (subResp) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(150 * (attempt + 1)));
    }
    if (!subResp) {
        NVR_WARN("onvif", "CreatePullPointSubscription failed after retries for %s",
                 events_service_url.c_str());
        return out;
    }
    auto addrOpt = extractSubscriptionAddress(*subResp);
    if (!addrOpt || addrOpt->empty()) {
        NVR_WARN("onvif", "no SubscriptionReference Address in CreatePullPointSubscription response");
        return out;
    }
    const std::string pullUrl = absolutePullUrl(events_service_url, *addrOpt);

    const char* kActPull = "http://www.onvif.org/ver10/events/wsdl/PullMessages";
    const std::string bodyPull =
        "<tev:PullMessages xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\">"
        "<tev:Timeout>PT5S</tev:Timeout>"
        "<tev:MessageLimit>16</tev:MessageLimit>"
        "</tev:PullMessages>";

    SoapClient               pullCli(pullUrl, user, pass);
    std::optional<std::string> pullResp;
    for (int attempt = 0; attempt < 3; ++attempt) {
        pullResp = pullCli.callEvents(pullUrl, kActPull, bodyPull);
        if (pullResp) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(150 * (attempt + 1)));
    }
    if (!pullResp) {
        NVR_WARN("onvif", "PullMessages failed after retries for %s", pullUrl.c_str());
        return out;
    }
    appendMessagesFromPullResponse(*pullResp, out);
    return out;
}

} // namespace nvr::onvif
