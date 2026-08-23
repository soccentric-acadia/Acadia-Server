// tests/unit/util/CronTest.cc
#include "util/Cron.h"
#include <gtest/gtest.h>
#include <string>

using namespace acadia::util;

// cron_next_occurrence(expression, from) — next fire strictly after `from`.
TEST(CronTest, EveryMinute) {
    EXPECT_EQ(cron_next_occurrence("* * * * *", "2026-01-02T15:04:00Z"),
              "2026-01-02T15:05:00Z");
}

TEST(CronTest, EveryFiveMinutesAlignsToStep) {
    EXPECT_EQ(cron_next_occurrence("*/5 * * * *", "2026-01-02T03:07:00Z"),
              "2026-01-02T03:10:00Z");
}

TEST(CronTest, SameMinuteIsStrictlyAfter) {
    // If `from` is exactly on the minute, the next fire is 24h later.
    EXPECT_EQ(cron_next_occurrence("30 14 * * *", "2026-01-02T14:30:00Z"),
              "2026-01-03T14:30:00Z");
}

TEST(CronTest, FirstOfMonth) {
    EXPECT_EQ(cron_next_occurrence("0 0 1 * *", "2026-01-15T00:00:00Z"),
              "2026-02-01T00:00:00Z");
}

TEST(CronTest, FirstOfJanuaryNextYear) {
    EXPECT_EQ(cron_next_occurrence("0 0 1 1 *", "2026-06-15T00:00:00Z"),
              "2027-01-01T00:00:00Z");
}

TEST(CronTest, MondayNoon) {
    // 2026-01-05 is a Monday; next Monday-noon after Friday 2026-01-02.
    EXPECT_EQ(cron_next_occurrence("0 12 * * 1", "2026-01-02T00:00:00Z"),
              "2026-01-05T12:00:00Z");
}

TEST(CronTest, SundayMidnight) {
    // 2026-01-04 is a Sunday.
    EXPECT_EQ(cron_next_occurrence("0 0 * * 0", "2026-01-02T00:00:00Z"),
              "2026-01-04T00:00:00Z");
}

TEST(CronTest, DomOrDowMatchesEither) {
    // Day-of-month 1 OR day-of-week Friday. 2026-01-01 (Thursday) matches via
    // DOW (it is a Friday? no — via DOW the first Friday after is 01-02, but
    // 01-01 itself matches dom=1). Start from 2026-01-02 (Friday) to exercise
    // the day-of-week branch: matches at 06:00 the same day.
    EXPECT_EQ(cron_next_occurrence("0 6 1 * 5", "2026-01-01T00:00:00Z"),
              "2026-01-01T06:00:00Z");
    EXPECT_EQ(cron_next_occurrence("0 6 1 * 5", "2026-01-02T00:00:00Z"),
              "2026-01-02T06:00:00Z");
}

TEST(CronTest, InvalidExpressionReturnsEmpty) {
    EXPECT_TRUE(cron_next_occurrence("not a cron", "2026-01-02T00:00:00Z").empty());
    EXPECT_TRUE(cron_next_occurrence("60 * * * *", "2026-01-02T00:00:00Z").empty());
    EXPECT_TRUE(cron_next_occurrence("* * * *", "2026-01-02T00:00:00Z").empty());
}
