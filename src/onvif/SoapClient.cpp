#include "nvr/onvif/SoapClient.hpp"

#include "nvr/Logger.hpp"

#include <curl/curl.h>
#include <sodium.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>

namespace nvr::onvif {

namespace {

size_t writeCb(char* ptr, size_t size, size_t nmemb, std::string* out) {
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string base64(const unsigned char* in, size_t len) {
    size_t bsize = sodium_base64_encoded_len(len, sodium_base64_VARIANT_ORIGINAL);
    std::string out(bsize, '\0');
    sodium_bin2base64(out.data(), bsize,
                       in, len, sodium_base64_VARIANT_ORIGINAL);
    out.resize(std::strlen(out.c_str()));
    return out;
}

std::string randomNonce() {
    unsigned char b[16];
    randombytes_buf(b, sizeof(b));
    return base64(b, sizeof(b));
}

std::string isoNow() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string sha1Digest(const std::string& nonce_b64, const std::string& created,
                       const std::string& pass) {
    unsigned char nonce[64];
    size_t nlen = 0;
    sodium_base642bin(nonce, sizeof(nonce), nonce_b64.c_str(), nonce_b64.size(),
                      nullptr, &nlen, nullptr, sodium_base64_VARIANT_ORIGINAL);

    std::string blob;
    blob.append(reinterpret_cast<const char*>(nonce), nlen);
    blob.append(created);
    blob.append(pass);

    crypto_hash_sha1_state st;
    crypto_hash_sha1_init(&st);
    crypto_hash_sha1_update(&st, reinterpret_cast<const unsigned char*>(blob.data()),
                             blob.size());
    unsigned char out[20];
    crypto_hash_sha1_final(&st, out);
    return base64(out, sizeof(out));
}

std::string buildSecurityHeader(const std::string& user, const std::string& pass) {
    if (user.empty() && pass.empty()) return {};
    auto nonce   = randomNonce();
    auto created = isoNow();
    auto digest  = sha1Digest(nonce, created, pass);

    std::ostringstream oss;
    oss << "<s:Header>"
        << "<wsse:Security xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/"
           "oasis-200401-wss-wssecurity-secext-1.0.xsd\" "
           "xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/"
           "oasis-200401-wss-wssecurity-utility-1.0.xsd\">"
        << "<wsse:UsernameToken>"
        << "<wsse:Username>" << user << "</wsse:Username>"
        << "<wsse:Password Type=\"http://docs.oasis-open.org/wss/2004/01/"
           "oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">"
        << digest << "</wsse:Password>"
        << "<wsse:Nonce EncodingType=\"http://docs.oasis-open.org/wss/2004/01/"
           "oasis-200401-wss-soap-message-security-1.0#Base64Binary\">"
        << nonce << "</wsse:Nonce>"
        << "<wsu:Created>" << created << "</wsu:Created>"
        << "</wsse:UsernameToken>"
        << "</wsse:Security>"
        << "</s:Header>";
    return oss.str();
}

}

SoapClient::SoapClient(std::string endpoint, std::string user, std::string password)
    : endpoint_(std::move(endpoint)), user_(std::move(user)), password_(std::move(password)) {}

std::optional<std::string> SoapClient::postEnvelope_(const std::string& envelope,
                                                     const std::string& soap_action) {
    CURL* c = curl_easy_init();
    if (!c) return std::nullopt;

    std::string response;
    struct curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, ("SOAPAction: \"" + soap_action + "\"").c_str());
    hdrs = curl_slist_append(hdrs, "Content-Type: application/soap+xml; charset=utf-8");

    curl_easy_setopt(c, CURLOPT_URL, endpoint_.c_str());
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, envelope.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(envelope.size()));
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);

    CURLcode rc = curl_easy_perform(c);
    long       http_status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) {
        NVR_WARN("onvif", "curl %s: %s", endpoint_.c_str(), curl_easy_strerror(rc));
        return std::nullopt;
    }
    if (http_status >= 400) {
        NVR_WARN("onvif", "HTTP %ld for %s", http_status, endpoint_.c_str());
        return std::nullopt;
    }
    return response;
}

std::optional<std::string> SoapClient::call(const std::string& action,
                                            const std::string& body_xml) {
    std::ostringstream env;
    env << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        << "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
           "xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\" "
           "xmlns:tt=\"http://www.onvif.org/ver10/schema\" "
           "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" "
           "xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\">"
        << buildSecurityHeader(user_, password_)
        << "<s:Body>" << body_xml << "</s:Body>"
        << "</s:Envelope>";
    return postEnvelope_(env.str(), action);
}

std::optional<std::string> SoapClient::getCapabilitiesXml() {
    return call("http://www.onvif.org/ver10/device/wsdl/GetCapabilities",
                "<tds:GetCapabilities><tds:Category>All</tds:Category></tds:GetCapabilities>");
}

std::optional<std::string> SoapClient::callEvents(const std::string& events_endpoint,
                                                 const std::string& action,
                                                 const std::string& body_xml) {
    SoapClient cli(events_endpoint, user_, password_);
    std::ostringstream env;
    env << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        << "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
           "xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\" "
           "xmlns:wsnt=\"http://docs.oasis-open.org/wsn/b-2\" "
           "xmlns:tt=\"http://www.onvif.org/ver10/schema\" "
           "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" "
           "xmlns:wsa5=\"http://www.w3.org/2005/08/addressing\">"
        << buildSecurityHeader(cli.user_, cli.password_)
        << "<s:Body>" << body_xml << "</s:Body>"
        << "</s:Envelope>";
    return cli.postEnvelope_(env.str(), action);
}

namespace {
std::string findText(const std::string& xml, const std::string& tag) {
    std::vector<std::string> variants{
        tag, "tt:" + tag, "trt:" + tag, "tds:" + tag, "tptz:" + tag
    };
    for (const auto& t : variants) {
        auto open  = "<" + t + ">";
        auto close = "</" + t + ">";
        auto a = xml.find(open);
        if (a == std::string::npos) continue;
        a += open.size();
        auto b = xml.find(close, a);
        if (b == std::string::npos) continue;
        return xml.substr(a, b - a);
    }
    return {};
}
}

std::optional<DeviceInfo> SoapClient::getDeviceInformation() {
    auto resp = call("http://www.onvif.org/ver10/device/wsdl/GetDeviceInformation",
                     "<tds:GetDeviceInformation/>");
    if (!resp) return std::nullopt;
    DeviceInfo info;
    info.manufacturer = findText(*resp, "Manufacturer");
    info.model        = findText(*resp, "Model");
    info.firmware     = findText(*resp, "FirmwareVersion");
    info.serial       = findText(*resp, "SerialNumber");
    return info;
}

std::vector<Profile> SoapClient::getProfiles(const std::string& media_endpoint) {
    SoapClient client(media_endpoint, user_, password_);
    auto resp = client.call("http://www.onvif.org/ver10/media/wsdl/GetProfiles",
                             "<trt:GetProfiles/>");
    std::vector<Profile> out;
    if (!resp) return out;

    size_t pos = 0;
    while ((pos = resp->find("<trt:Profiles", pos)) != std::string::npos) {
        auto end = resp->find("</trt:Profiles>", pos);
        if (end == std::string::npos) break;
        auto chunk = resp->substr(pos, end - pos);
        Profile p;
        auto tok_pos = chunk.find("token=\"");
        if (tok_pos != std::string::npos) {
            tok_pos += 7;
            auto e = chunk.find('"', tok_pos);
            p.token = chunk.substr(tok_pos, e - tok_pos);
        }
        p.name = findText(chunk, "Name");
        try { p.width  = std::stoi(findText(chunk, "Width")); } catch (...) {}
        try { p.height = std::stoi(findText(chunk, "Height")); } catch (...) {}
        p.codec = findText(chunk, "Encoding");
        out.push_back(p);
        pos = end + 1;
    }
    return out;
}

std::optional<std::string> SoapClient::getStreamUri(const std::string& media_endpoint,
                                                     const std::string& profile_token) {
    SoapClient client(media_endpoint, user_, password_);
    std::ostringstream body;
    body << "<trt:GetStreamUri>"
         << "<trt:StreamSetup>"
         << "<tt:Stream>RTP-Unicast</tt:Stream>"
         << "<tt:Transport><tt:Protocol>RTSP</tt:Protocol></tt:Transport>"
         << "</trt:StreamSetup>"
         << "<trt:ProfileToken>" << profile_token << "</trt:ProfileToken>"
         << "</trt:GetStreamUri>";
    auto resp = client.call("http://www.onvif.org/ver10/media/wsdl/GetStreamUri", body.str());
    if (!resp) return std::nullopt;
    auto uri = findText(*resp, "Uri");
    if (uri.empty()) return std::nullopt;
    return uri;
}

bool SoapClient::ptzMove(const std::string& ptz_endpoint, const std::string& profile_token,
                          const PtzVector& v) {
    SoapClient client(ptz_endpoint, user_, password_);
    std::ostringstream body;
    body << "<tptz:ContinuousMove>"
         << "<tptz:ProfileToken>" << profile_token << "</tptz:ProfileToken>"
         << "<tptz:Velocity>"
         << "<tt:PanTilt x=\"" << v.pan << "\" y=\"" << v.tilt << "\"/>"
         << "<tt:Zoom x=\"" << v.zoom << "\"/>"
         << "</tptz:Velocity>"
         << "</tptz:ContinuousMove>";
    return client.call("http://www.onvif.org/ver20/ptz/wsdl/ContinuousMove", body.str()).has_value();
}

bool SoapClient::ptzStop(const std::string& ptz_endpoint, const std::string& profile_token) {
    SoapClient client(ptz_endpoint, user_, password_);
    std::ostringstream body;
    body << "<tptz:Stop>"
         << "<tptz:ProfileToken>" << profile_token << "</tptz:ProfileToken>"
         << "<tptz:PanTilt>true</tptz:PanTilt>"
         << "<tptz:Zoom>true</tptz:Zoom>"
         << "</tptz:Stop>";
    return client.call("http://www.onvif.org/ver20/ptz/wsdl/Stop", body.str()).has_value();
}

bool SoapClient::setSystemDateAndTimeUtc(std::chrono::system_clock::time_point utc_tp) {
    const std::time_t t = std::chrono::system_clock::to_time_t(utc_tp);
    std::tm             tm{};
    gmtime_r(&t, &tm);
    const int year  = tm.tm_year + 1900;
    const int month = tm.tm_mon + 1;
    const int day   = tm.tm_mday;
    std::ostringstream body;
    body << "<tds:SetSystemDateAndTime>"
         << "<tds:DateTimeType>Manual</tds:DateTimeType>"
         << "<tds:DaylightSavings>false</tds:DaylightSavings>"
         << "<tds:TimeZone><tt:TZ></tt:TZ></tds:TimeZone>"
         << "<tds:UTCDateTime>"
         << "<tt:Time><tt:Hour>" << tm.tm_hour << "</tt:Hour><tt:Minute>" << tm.tm_min
         << "</tt:Minute><tt:Second>" << tm.tm_sec << "</tt:Second></tt:Time>"
         << "<tt:Date><tt:Year>" << year << "</tt:Year><tt:Month>" << month << "</tt:Month><tt:Day>"
         << day << "</tt:Day></tt:Date>"
         << "</tds:UTCDateTime>"
         << "</tds:SetSystemDateAndTime>";
    auto resp = call("http://www.onvif.org/ver10/device/wsdl/SetSystemDateAndTime", body.str());
    return resp.has_value();
}

}
