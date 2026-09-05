#include <gtest/gtest.h>

#include "px_common_new/time_util.h"

namespace px {

TEST(TimeUtilTest, WallClockFormattingIsAvailableAndStable) {
    EXPECT_FALSE(TimeUtil::FormatTimestamp(0).empty());
    EXPECT_FALSE(TimeUtil::FormatTimestamp2(0).empty());
    EXPECT_TRUE(TimeUtil::FormatTimestamp(7, true).ends_with(".7"));
}

TEST(TimeUtilTest, ElapsedClockIsMonotonic) {
    const auto before = TimeUtil::GetCurrentTimePointUS();
    TimeUtil::DelayBySleep(2);
    const auto after = TimeUtil::GetCurrentTimePointUS();
    EXPECT_GE(after, before);
}

TEST(TimeUtilTest, DurationFormattingHandlesDays) {
    EXPECT_EQ(TimeUtil::FormatSecondsToDHMS(90061), "1D 01:01:01");
}

}  // namespace px
