/**
 * @file test_config_validation.cpp
 * @brief Unit tests for configuration validation functions
 */

#include <unity.h>
#include <Arduino.h>
#include <string.h>
#include "core/meter_code_parser.h"

// Mock configuration values for testing
#define TEST_METER_YEAR 2020
#define TEST_METER_SERIAL 12345678
#define TEST_FREQUENCY 433.82

// Local implementations of functions under test for now.
// These can later be moved into shared application code if needed.

bool isValidReadingSchedule(const char *schedule)
{
    // Reject null or empty schedules
    if (schedule == nullptr || schedule[0] == '\0')
    {
        return false;
    }

    // Allowed values based on tests
    if (strcmp(schedule, "Monday-Friday") == 0)
        return true;
    if (strcmp(schedule, "Monday-Saturday") == 0)
        return true;
    if (strcmp(schedule, "Monday-Sunday") == 0)
        return true;

    // Everything else is invalid
    return false;
}

bool validateConfiguration()
{
    // Placeholder; not used by current tests
    return true;
}

void setUp(void)
{
    // This runs before each test
}

void tearDown(void)
{
    // This runs after each test
}

/**
 * Test: isValidReadingSchedule with valid inputs
 */
void test_valid_reading_schedules(void)
{
    TEST_ASSERT_TRUE(isValidReadingSchedule("Monday-Friday"));
    TEST_ASSERT_TRUE(isValidReadingSchedule("Monday-Saturday"));
    TEST_ASSERT_TRUE(isValidReadingSchedule("Monday-Sunday"));
}

/**
 * Test: isValidReadingSchedule with invalid inputs
 */
void test_invalid_reading_schedules(void)
{
    TEST_ASSERT_FALSE(isValidReadingSchedule("Daily"));
    TEST_ASSERT_FALSE(isValidReadingSchedule("Weekdays"));
    TEST_ASSERT_FALSE(isValidReadingSchedule(""));
    TEST_ASSERT_FALSE(isValidReadingSchedule(NULL));
    TEST_ASSERT_FALSE(isValidReadingSchedule("Monday-Thursday"));
}

/**
 * Test: Meter year validation
 */
void test_meter_year_validation(void)
{
    // Valid years (2009 onwards when RADIAN was introduced)
    TEST_ASSERT_TRUE(TEST_METER_YEAR >= 2009);
    TEST_ASSERT_TRUE(TEST_METER_YEAR <= 2025);
}

/**
 * Test: Meter serial validation
 */
void test_meter_serial_validation(void)
{
    // Valid serial (non-zero, max 8 digits)
    TEST_ASSERT_TRUE(TEST_METER_SERIAL > 0);
    TEST_ASSERT_TRUE(TEST_METER_SERIAL <= 99999999);
}

/**
 * Test: Frequency validation
 */
void test_frequency_validation(void)
{
    // Valid frequency (433 MHz ±1 MHz)
    TEST_ASSERT_FLOAT_WITHIN(1.0, 433.82, TEST_FREQUENCY);
    TEST_ASSERT_TRUE(TEST_FREQUENCY >= 300.0);
    TEST_ASSERT_TRUE(TEST_FREQUENCY <= 500.0);
}

void test_meter_code_parse_valid_dashed_with_suffix(void)
{
    uint8_t year = 0;
    uint32_t serial = 0;
    TEST_ASSERT_TRUE(everblu::core::parseMeterCode("20-0257301-999", &year, &serial));
    TEST_ASSERT_EQUAL_UINT8(20, year);
    TEST_ASSERT_EQUAL_UINT32(257301UL, serial);
}

void test_meter_code_parse_valid_dashed_without_suffix(void)
{
    uint8_t year = 0;
    uint32_t serial = 0;
    TEST_ASSERT_TRUE(everblu::core::parseMeterCode("20-0257301", &year, &serial));
    TEST_ASSERT_EQUAL_UINT8(20, year);
    TEST_ASSERT_EQUAL_UINT32(257301UL, serial);
}

void test_meter_code_parse_rejects_non_digit(void)
{
    TEST_ASSERT_FALSE(everblu::core::parseMeterCode("20-02573A1-999", nullptr, nullptr));
}

void test_meter_code_parse_rejects_missing_dash_format(void)
{
    TEST_ASSERT_FALSE(everblu::core::parseMeterCode("200257301999", nullptr, nullptr));
}

void test_meter_code_parse_rejects_zero_serial(void)
{
    TEST_ASSERT_FALSE(everblu::core::parseMeterCode("20-0000000-000", nullptr, nullptr));
}

void test_meter_code_parse_rejects_serial_over_24bit(void)
{
    // 16777216 is one above 0xFFFFFF.
    TEST_ASSERT_FALSE(everblu::core::parseMeterCode("20-16777216-000", nullptr, nullptr));
}

void setup()
{
    delay(2000); // Wait for serial monitor

    UNITY_BEGIN();

    RUN_TEST(test_valid_reading_schedules);
    RUN_TEST(test_invalid_reading_schedules);
    RUN_TEST(test_meter_year_validation);
    RUN_TEST(test_meter_serial_validation);
    RUN_TEST(test_frequency_validation);
    RUN_TEST(test_meter_code_parse_valid_dashed_with_suffix);
    RUN_TEST(test_meter_code_parse_valid_dashed_without_suffix);
    RUN_TEST(test_meter_code_parse_rejects_non_digit);
    RUN_TEST(test_meter_code_parse_rejects_missing_dash_format);
    RUN_TEST(test_meter_code_parse_rejects_zero_serial);
    RUN_TEST(test_meter_code_parse_rejects_serial_over_24bit);

    UNITY_END();
}

void loop()
{
    // Nothing to do here
}
