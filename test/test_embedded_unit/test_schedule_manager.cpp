/**
 * @file test_schedule_manager.cpp
 * @brief Unit tests for ScheduleManager - schedule logic, timezone conversion, auto-alignment
 *
 * Test registration lives in test_runner.cpp.
 */

#include <unity.h>
#include <ctime>
#include <cstring>
#include "services/schedule_manager.h"

// Wrapper to drive ScheduleManager using the legacy isReadingDay signature
static bool isReadingDay(struct tm *ptm, const char *schedule)
{
    ScheduleManager::setSchedule(schedule);
    return ScheduleManager::isReadingDay(ptm);
}

/**
 * Helper to create a tm structure for a specific day of week
 * @param dayOfWeek 0 = Sunday, 1 = Monday, ..., 6 = Saturday
 */
struct tm createTmForDay(int dayOfWeek)
{
    struct tm timeinfo = {};
    timeinfo.tm_wday = dayOfWeek;
    timeinfo.tm_year = 125; // 2025
    timeinfo.tm_mon = 1;    // February
    timeinfo.tm_mday = 10;  // Arbitrary date
    timeinfo.tm_hour = 12;
    timeinfo.tm_min = 0;
    timeinfo.tm_sec = 0;
    return timeinfo;
}

/**
 * Test: Monday-Friday schedule includes all weekdays
 */
void test_schedule_monday_friday(void)
{
    struct tm monday = createTmForDay(1);
    struct tm tuesday = createTmForDay(2);
    struct tm wednesday = createTmForDay(3);
    struct tm thursday = createTmForDay(4);
    struct tm friday = createTmForDay(5);
    struct tm saturday = createTmForDay(6);
    struct tm sunday = createTmForDay(0);

    TEST_ASSERT_TRUE(isReadingDay(&monday, "Monday-Friday"));
    TEST_ASSERT_TRUE(isReadingDay(&tuesday, "Monday-Friday"));
    TEST_ASSERT_TRUE(isReadingDay(&wednesday, "Monday-Friday"));
    TEST_ASSERT_TRUE(isReadingDay(&thursday, "Monday-Friday"));
    TEST_ASSERT_TRUE(isReadingDay(&friday, "Monday-Friday"));
    TEST_ASSERT_FALSE(isReadingDay(&saturday, "Monday-Friday"));
    TEST_ASSERT_FALSE(isReadingDay(&sunday, "Monday-Friday"));
}

/**
 * Test: Monday-Saturday schedule includes Monday through Saturday
 */
void test_schedule_monday_saturday(void)
{
    struct tm monday = createTmForDay(1);
    struct tm tuesday = createTmForDay(2);
    struct tm saturday = createTmForDay(6);
    struct tm sunday = createTmForDay(0);

    TEST_ASSERT_TRUE(isReadingDay(&monday, "Monday-Saturday"));
    TEST_ASSERT_TRUE(isReadingDay(&tuesday, "Monday-Saturday"));
    TEST_ASSERT_TRUE(isReadingDay(&saturday, "Monday-Saturday"));
    TEST_ASSERT_FALSE(isReadingDay(&sunday, "Monday-Saturday"));
}

/**
 * Test: Monday-Sunday schedule includes ALL days (CRITICAL - this was the bug)
 * Sunday (dayOfWeek = 0) MUST be included
 */
void test_schedule_monday_sunday_includes_sunday(void)
{
    struct tm monday = createTmForDay(1);
    struct tm tuesday = createTmForDay(2);
    struct tm wednesday = createTmForDay(3);
    struct tm thursday = createTmForDay(4);
    struct tm friday = createTmForDay(5);
    struct tm saturday = createTmForDay(6);
    struct tm sunday = createTmForDay(0);

    // All days must be true for Monday-Sunday schedule
    TEST_ASSERT_TRUE(isReadingDay(&monday, "Monday-Sunday"));
    TEST_ASSERT_TRUE(isReadingDay(&tuesday, "Monday-Sunday"));
    TEST_ASSERT_TRUE(isReadingDay(&wednesday, "Monday-Sunday"));
    TEST_ASSERT_TRUE(isReadingDay(&thursday, "Monday-Sunday"));
    TEST_ASSERT_TRUE(isReadingDay(&friday, "Monday-Sunday"));
    TEST_ASSERT_TRUE(isReadingDay(&saturday, "Monday-Sunday"));

    // CRITICAL: Sunday (dayOfWeek = 0) MUST return true
    // This was the bug: previous code returned (dayOfWeek != 0) which excluded Sunday
    TEST_ASSERT_TRUE(isReadingDay(&sunday, "Monday-Sunday"));
}

/**
 * Test: Monday schedule includes Monday
 */
void test_schedule_monday_only(void)
{
    struct tm monday = createTmForDay(1);
    struct tm tuesday = createTmForDay(2);
    struct tm saturday = createTmForDay(6);
    struct tm sunday = createTmForDay(0);

    TEST_ASSERT_TRUE(isReadingDay(&monday, "Monday"));
    TEST_ASSERT_FALSE(isReadingDay(&tuesday, "Monday"));
    TEST_ASSERT_FALSE(isReadingDay(&saturday, "Monday"));
    TEST_ASSERT_FALSE(isReadingDay(&sunday, "Monday"));
}

/**
 * Test: Tuesday schedule includes Tuesday
 */
void test_schedule_tuesday_only(void)
{
    struct tm monday = createTmForDay(1);
    struct tm tuesday = createTmForDay(2);
    struct tm saturday = createTmForDay(6);
    struct tm sunday = createTmForDay(0);

    TEST_ASSERT_FALSE(isReadingDay(&monday, "Tuesday"));
    TEST_ASSERT_TRUE(isReadingDay(&tuesday, "Tuesday"));
    TEST_ASSERT_FALSE(isReadingDay(&saturday, "Tuesday"));
    TEST_ASSERT_FALSE(isReadingDay(&sunday, "Tuesday"));
}

/**
 * Test: Wednesday schedule includes Wednesday
 */
void test_schedule_wednesday_only(void)
{
    struct tm monday = createTmForDay(1);
    struct tm wednesday = createTmForDay(3);
    struct tm saturday = createTmForDay(6);
    struct tm sunday = createTmForDay(0);

    TEST_ASSERT_FALSE(isReadingDay(&monday, "Wednesday"));
    TEST_ASSERT_TRUE(isReadingDay(&wednesday, "Wednesday"));
    TEST_ASSERT_FALSE(isReadingDay(&saturday, "Wednesday"));
    TEST_ASSERT_FALSE(isReadingDay(&sunday, "Wednesday"));
}

/**
 * Test: Thursday schedule includes Thursday
 */
void test_schedule_thursday_only(void)
{
    struct tm monday = createTmForDay(1);
    struct tm thursday = createTmForDay(4);
    struct tm saturday = createTmForDay(6);
    struct tm sunday = createTmForDay(0);

    TEST_ASSERT_FALSE(isReadingDay(&monday, "Thursday"));
    TEST_ASSERT_TRUE(isReadingDay(&thursday, "Thursday"));
    TEST_ASSERT_FALSE(isReadingDay(&saturday, "Thursday"));
    TEST_ASSERT_FALSE(isReadingDay(&sunday, "Thursday"));
}

/**
 * Test: Friday schedule includes Friday
 */
void test_schedule_friday_only(void)
{
    struct tm monday = createTmForDay(1);
    struct tm friday = createTmForDay(5);
    struct tm saturday = createTmForDay(6);
    struct tm sunday = createTmForDay(0);

    TEST_ASSERT_FALSE(isReadingDay(&monday, "Friday"));
    TEST_ASSERT_TRUE(isReadingDay(&friday, "Friday"));
    TEST_ASSERT_FALSE(isReadingDay(&saturday, "Friday"));
    TEST_ASSERT_FALSE(isReadingDay(&sunday, "Friday"));
}

/**
 * Test: Saturday schedule includes Saturday
 */
void test_schedule_saturday_only(void)
{
    struct tm monday = createTmForDay(1);
    struct tm saturday = createTmForDay(6);
    struct tm sunday = createTmForDay(0);

    TEST_ASSERT_FALSE(isReadingDay(&monday, "Saturday"));
    TEST_ASSERT_TRUE(isReadingDay(&saturday, "Saturday"));
    TEST_ASSERT_FALSE(isReadingDay(&sunday, "Saturday"));
}

/**
 * Test: Sunday schedule includes Sunday
 */
void test_schedule_sunday_only(void)
{
    struct tm monday = createTmForDay(1);
    struct tm saturday = createTmForDay(6);
    struct tm sunday = createTmForDay(0);

    TEST_ASSERT_FALSE(isReadingDay(&monday, "Sunday"));
    TEST_ASSERT_FALSE(isReadingDay(&saturday, "Sunday"));
    TEST_ASSERT_TRUE(isReadingDay(&sunday, "Sunday"));
}

/**
 * Test: Invalid schedule falls back to Monday-Friday schedule
 */
void test_schedule_invalid(void)
{
    struct tm monday = createTmForDay(1);
    struct tm sunday = createTmForDay(0);

    // Invalid schedule string should be treated as Monday-Friday:
    // weekdays are reading days, Sunday is not.
    TEST_ASSERT_TRUE(isReadingDay(&monday, "InvalidSchedule"));
    TEST_ASSERT_FALSE(isReadingDay(&sunday, "InvalidSchedule"));
}

/**
 * Test: Empty schedule string falls back to Monday-Friday
 */
void test_schedule_empty(void)
{
    struct tm monday = createTmForDay(1);
    struct tm sunday = createTmForDay(0);

    TEST_ASSERT_TRUE(isReadingDay(&monday, ""));
    TEST_ASSERT_FALSE(isReadingDay(&sunday, ""));
}

/**
 * Test: Null schedule string must not crash and falls back to Monday-Friday
 */
void test_schedule_null(void)
{
    struct tm monday = createTmForDay(1);
    struct tm sunday = createTmForDay(0);

    TEST_ASSERT_TRUE(isReadingDay(&monday, nullptr));
    TEST_ASSERT_FALSE(isReadingDay(&sunday, nullptr));
}

/**
 * Test: Null tm pointer must not crash and is never a reading day
 */
void test_schedule_null_tm_is_not_a_reading_day(void)
{
    ScheduleManager::setSchedule("Monday-Sunday");
    TEST_ASSERT_FALSE(ScheduleManager::isReadingDay(nullptr));
}

/**
 * Test: All days of week for each schedule type (comprehensive)
 */
void test_all_schedules_all_days(void)
{
    const char *schedules[] = {
        "Monday-Friday",
        "Monday-Saturday",
        "Monday-Sunday"};

    const int expectedMonday[] = {1, 1, 1};    // Mon: yes, yes, yes
    const int expectedTuesday[] = {1, 1, 1};   // Tue: yes, yes, yes
    const int expectedWednesday[] = {1, 1, 1}; // Wed: yes, yes, yes
    const int expectedThursday[] = {1, 1, 1};  // Thu: yes, yes, yes
    const int expectedFriday[] = {1, 1, 1};    // Fri: yes, yes, yes
    const int expectedSaturday[] = {0, 1, 1};  // Sat: no, yes, yes
    const int expectedSunday[] = {0, 0, 1};    // Sun: no, no, yes

    struct tm monday = createTmForDay(1);
    struct tm tuesday = createTmForDay(2);
    struct tm wednesday = createTmForDay(3);
    struct tm thursday = createTmForDay(4);
    struct tm friday = createTmForDay(5);
    struct tm saturday = createTmForDay(6);
    struct tm sunday = createTmForDay(0);

    for (int s = 0; s < 3; s++)
    {
        TEST_ASSERT_EQUAL(expectedMonday[s], isReadingDay(&monday, schedules[s]));
        TEST_ASSERT_EQUAL(expectedTuesday[s], isReadingDay(&tuesday, schedules[s]));
        TEST_ASSERT_EQUAL(expectedWednesday[s], isReadingDay(&wednesday, schedules[s]));
        TEST_ASSERT_EQUAL(expectedThursday[s], isReadingDay(&thursday, schedules[s]));
        TEST_ASSERT_EQUAL(expectedFriday[s], isReadingDay(&friday, schedules[s]));
        TEST_ASSERT_EQUAL(expectedSaturday[s], isReadingDay(&saturday, schedules[s]));
        TEST_ASSERT_EQUAL(expectedSunday[s], isReadingDay(&sunday, schedules[s]));
    }
}

/**
 * Test: UTC reading time is converted to local using a positive (east) offset
 */
void test_reading_time_utc_to_local_positive_offset(void)
{
    ScheduleManager::begin("Monday-Friday", 22, 30, 120); // UTC+2

    TEST_ASSERT_EQUAL_INT(22, ScheduleManager::getReadingHourUtc());
    TEST_ASSERT_EQUAL_INT(30, ScheduleManager::getReadingMinuteUtc());
    // 22:30 UTC + 2h wraps past midnight to 00:30 local
    TEST_ASSERT_EQUAL_INT(0, ScheduleManager::getReadingHourLocal());
    TEST_ASSERT_EQUAL_INT(30, ScheduleManager::getReadingMinuteLocal());
    TEST_ASSERT_EQUAL_INT(120, ScheduleManager::getTimezoneOffsetMinutes());
}

/**
 * Test: UTC reading time is converted to local using a negative (west) offset
 */
void test_reading_time_utc_to_local_negative_offset(void)
{
    ScheduleManager::begin("Monday-Friday", 1, 15, -330); // UTC-5:30

    // 01:15 UTC - 5:30 wraps back to 19:45 local on the previous day
    TEST_ASSERT_EQUAL_INT(19, ScheduleManager::getReadingHourLocal());
    TEST_ASSERT_EQUAL_INT(45, ScheduleManager::getReadingMinuteLocal());
}

/**
 * Test: setting the local reading time round-trips back through UTC
 */
void test_reading_time_local_to_utc_roundtrip(void)
{
    ScheduleManager::begin("Monday-Friday", 10, 0, 60); // UTC+1
    ScheduleManager::setReadingTimeFromLocal(9, 45);

    TEST_ASSERT_EQUAL_INT(9, ScheduleManager::getReadingHourLocal());
    TEST_ASSERT_EQUAL_INT(45, ScheduleManager::getReadingMinuteLocal());
    TEST_ASSERT_EQUAL_INT(8, ScheduleManager::getReadingHourUtc());
    TEST_ASSERT_EQUAL_INT(45, ScheduleManager::getReadingMinuteUtc());
}

/**
 * Test: out-of-range reading times are clamped rather than wrapped
 */
void test_reading_time_is_clamped(void)
{
    ScheduleManager::begin("Monday-Friday", 99, 99, 0);
    TEST_ASSERT_EQUAL_INT(23, ScheduleManager::getReadingHourUtc());
    TEST_ASSERT_EQUAL_INT(59, ScheduleManager::getReadingMinuteUtc());

    ScheduleManager::setReadingTimeFromUtc(-5, -5);
    TEST_ASSERT_EQUAL_INT(0, ScheduleManager::getReadingHourUtc());
    TEST_ASSERT_EQUAL_INT(0, ScheduleManager::getReadingMinuteUtc());
}

/**
 * Test: auto-alignment picks the midpoint of the meter wake window
 */
void test_auto_align_uses_window_midpoint(void)
{
    ScheduleManager::begin("Monday-Friday", 10, 0, 0);

    // Window 06:00-18:00 local is 12 hours wide, midpoint 12:00
    TEST_ASSERT_TRUE(ScheduleManager::autoAlignToMeterWindow(6, 18, true));
    TEST_ASSERT_EQUAL_INT(12, ScheduleManager::getReadingHourLocal());

    // Without midpoint, alignment lands on the start of the window
    TEST_ASSERT_TRUE(ScheduleManager::autoAlignToMeterWindow(6, 18, false));
    TEST_ASSERT_EQUAL_INT(6, ScheduleManager::getReadingHourLocal());
}

/**
 * Test: a zero-length meter window is rejected and leaves the time unchanged
 */
void test_auto_align_rejects_zero_length_window(void)
{
    ScheduleManager::begin("Monday-Friday", 10, 0, 0);

    TEST_ASSERT_FALSE(ScheduleManager::autoAlignToMeterWindow(8, 8, true));
    TEST_ASSERT_EQUAL_INT(10, ScheduleManager::getReadingHourLocal());
}
