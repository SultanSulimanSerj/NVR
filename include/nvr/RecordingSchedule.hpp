#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace nvr {

/// Canonical JSON when recording is allowed 24/7 (subject to motion mode etc.).
constexpr std::string_view kDefaultRecordingScheduleJson = R"({"always":true})";

/// Parse and validate schedule JSON. Returns compact normalized JSON or nullopt on invalid input.
std::optional<std::string> tryNormalizeRecordingScheduleJson(std::string_view raw);

/// Throws std::runtime_error with a short English message if invalid (for API layer).
std::string normalizeRecordingScheduleJsonOrThrow(std::string_view raw);

/// Whether local wall-clock time falls inside an allowed window (host timezone).
/// Invalid or empty `json_str` is treated as always-on (fail-open for recorder).
bool recordingScheduleAllowsLocalNow(std::string_view json_str) noexcept;

} // namespace nvr
