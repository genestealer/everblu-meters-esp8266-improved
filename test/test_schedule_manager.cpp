/**
 * @file test_schedule_manager.cpp
 * @brief Unit tests for ScheduleManager - validates schedule logic for all day combinations
 */

#include <unity.h>
#include <ctime>
#include <cstring>
#include "src/services/schedule_manager.h"

// Wrapper to drive ScheduleManager using the legacy isReadingDay signature
static ScheduleManager g_scheduleManager;

bool isReadingDay(struct tm *ptm, const char *schedule)
{
    g_scheduleManager.setSchedule(schedule);
    return g_scheduleManager.isReadingDay(ptm);
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
 * Test: Invalid schedule returns false for all days
 */
void test_schedule_invalid(void)
{
    struct tm monday = createTmForDay(1);
    struct tm sunday = createTmForDay(0);

    TEST_ASSERT_FALSE(isReadingDay(&monday, "InvalidSchedule"));
    TEST_ASSERT_FALSE(isReadingDay(&sunday, "InvalidSchedule"));
    TEST_ASSERT_FALSE(isReadingDay(&monday, "Everyday")); // Removed - use Monday-Sunday instead
    TEST_ASSERT_FALSE(isReadingDay(&sunday, "Everyday")); // Removed - use Monday-Sunday instead
    TEST_ASSERT_FALSE(isReadingDay(&monday, "Saturday")); // Removed - use Monday-Saturday instead
    TEST_ASSERT_FALSE(isReadingDay(&sunday, "Sunday"));   // Removed - use Monday-Sunday instead
}

/**
 * Test: Empty schedule string returns false
 */
void test_schedule_empty(void)
{
    struct tm monday = createTmForDay(1);
    TEST_ASSERT_FALSE(isReadingDay(&monday, ""));
}

/**
 * Test: Null schedule string handling
 */
void test_schedule_null(void)
{
    struct tm monday = createTmForDay(1);
    // Should not crash - returns false for null
    TEST_ASSERT_FALSE(isReadingDay(&monday, nullptr));
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

// Optional: Unity test runner setup
void setUp(void)
{
    // No setup needed for this test
}

void tearDown(void)
{
    // No teardown needed
}
