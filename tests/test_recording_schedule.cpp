#include "nvr/RecordingSchedule.hpp"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

using namespace nvr;

TEST(RecordingSchedule, DefaultAlways) {
    auto n = tryNormalizeRecordingScheduleJson("");
    ASSERT_TRUE(n);
    EXPECT_EQ(*n, R"({"always":true})");
}

TEST(RecordingSchedule, RejectBadJson) {
    EXPECT_FALSE(tryNormalizeRecordingScheduleJson("not json"));
    EXPECT_FALSE(tryNormalizeRecordingScheduleJson("[]"));
}

TEST(RecordingSchedule, AlwaysFalseRequiresWeekdays) {
    EXPECT_FALSE(tryNormalizeRecordingScheduleJson(R"({"always":false})"));
    auto n = tryNormalizeRecordingScheduleJson(
        R"({"always":false,"weekdays":{"mon":[{"start":"09:00","end":"17:00"}],"tue":[],"wed":[],"thu":[],"fri":[],"sat":[],"sun":[]}})");
    ASSERT_TRUE(n);
    auto j = nlohmann::json::parse(*n);
    EXPECT_FALSE(j["always"].get<bool>());
    EXPECT_EQ(j["weekdays"]["mon"].size(), 1u);
}

TEST(RecordingSchedule, RejectBadWindow) {
    EXPECT_FALSE(tryNormalizeRecordingScheduleJson(
        R"({"always":false,"weekdays":{"mon":[{"start":"17:00","end":"09:00"}],"tue":[],"wed":[],"thu":[],"fri":[],"sat":[],"sun":[]}})"));
    EXPECT_FALSE(tryNormalizeRecordingScheduleJson(
        R"({"always":false,"weekdays":{"mon":[{"start":"25:00","end":"26:00"}],"tue":[],"wed":[],"thu":[],"fri":[],"sat":[],"sun":[]}})"));
}
