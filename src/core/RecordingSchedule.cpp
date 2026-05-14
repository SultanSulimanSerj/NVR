#include "nvr/RecordingSchedule.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cctype>
#include <ctime>
#include <regex>

namespace nvr {

namespace {

bool parseHm(std::string_view s, int& out_min) {
    static const std::regex rx(R"((\d{1,2}):(\d{2}))");
    std::smatch m;
    std::string ss(s);
    if (!std::regex_match(ss, m, rx)) return false;
    int h = std::stoi(m[1].str());
    int mi = std::stoi(m[2].str());
    if (h < 0 || h > 23 || mi < 0 || mi > 59) return false;
    out_min = h * 60 + mi;
    return true;
}

constexpr std::array<const char*, 7> kWdayKeys = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};

} // namespace

std::optional<std::string> tryNormalizeRecordingScheduleJson(std::string_view raw) {
    if (raw.empty()) return std::string{kDefaultRecordingScheduleJson};

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(raw.begin(), raw.end());
    } catch (...) {
        return std::nullopt;
    }
    if (!j.is_object()) return std::nullopt;

    const bool always = j.value("always", true);
    if (always) {
        return std::string{R"({"always":true})"};
    }

    if (!j.contains("weekdays") || !j["weekdays"].is_object()) return std::nullopt;
    const auto& wd = j["weekdays"];

    nlohmann::json out_wd = nlohmann::json::object();
    for (const char* key : kWdayKeys) {
        if (!wd.contains(key)) {
            out_wd[key] = nlohmann::json::array();
            continue;
        }
        if (!wd[key].is_array()) return std::nullopt;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& win : wd[key]) {
            if (!win.is_object()) return std::nullopt;
            auto s = win.value("start", std::string{});
            auto e = win.value("end", std::string{});
            int sm = 0, em = 0;
            if (!parseHm(s, sm) || !parseHm(e, em)) return std::nullopt;
            if (sm >= em) return std::nullopt;
            arr.push_back(nlohmann::json{{"start", s}, {"end", e}});
        }
        out_wd[key] = std::move(arr);
    }

    nlohmann::json out = {{"always", false}, {"weekdays", std::move(out_wd)}};
    return out.dump();
}

std::string normalizeRecordingScheduleJsonOrThrow(std::string_view raw) {
    auto n = tryNormalizeRecordingScheduleJson(raw);
    if (!n) throw std::runtime_error("bad_recording_schedule");
    return *n;
}

bool recordingScheduleAllowsLocalNow(std::string_view json_str) noexcept {
    if (json_str.empty()) return true;

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_str.begin(), json_str.end());
    } catch (...) {
        return true;
    }
    if (!j.is_object()) return true;
    if (j.value("always", true)) return true;

    const auto& wd = j["weekdays"];
    if (!wd.is_object()) return true;

    const auto now = std::time(nullptr);
    std::tm    tm{};
    if (!localtime_r(&now, &tm)) return true;
    const int wday = tm.tm_wday; // 0=Sun .. 6=Sat
    if (wday < 0 || wday > 6) return true;
    const int cur = tm.tm_hour * 60 + tm.tm_min;

    const char* key = kWdayKeys[static_cast<size_t>(wday)];
    if (!wd.contains(key) || !wd[key].is_array()) return false;

    for (const auto& win : wd[key]) {
        if (!win.is_object()) continue;
        int sm = 0, em = 0;
        auto s = win.value("start", std::string{});
        auto e = win.value("end", std::string{});
        if (!parseHm(s, sm) || !parseHm(e, em)) continue;
        if (sm < em && cur >= sm && cur < em) return true;
    }
    return false;
}

} // namespace nvr
