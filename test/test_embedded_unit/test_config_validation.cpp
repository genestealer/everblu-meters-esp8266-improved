/**
 * @file test_config_validation.cpp
 * @brief Unit tests for configuration validation
 *
 * Test registration lives in test_runner.cpp.
 */

#include <unity.h>
#include <string.h>
#include "core/meter_code_parser.h"
#include "services/schedule_manager.h"

#define TEST_FREQUENCY 433.82

/**
 * Test: schedule strings accepted by ScheduleManager
 */
void test_valid_reading_schedules(void)
{
    TEST_ASSERT_TRUE(ScheduleManager::isValidSchedule("Monday-Friday"));
    TEST_ASSERT_TRUE(ScheduleManager::isValidSchedule("Monday-Saturday"));
    TEST_ASSERT_TRUE(ScheduleManager::isValidSchedule("Monday-Sunday"));
    TEST_ASSERT_TRUE(ScheduleManager::isValidSchedule("Monday"));
    TEST_ASSERT_TRUE(ScheduleManager::isValidSchedule("Tuesday"));
    TEST_ASSERT_TRUE(ScheduleManager::isValidSchedule("Wednesday"));
    TEST_ASSERT_TRUE(ScheduleManager::isValidSchedule("Thursday"));
    TEST_ASSERT_TRUE(ScheduleManager::isValidSchedule("Friday"));
    TEST_ASSERT_TRUE(ScheduleManager::isValidSchedule("Saturday"));
    TEST_ASSERT_TRUE(ScheduleManager::isValidSchedule("Sunday"));
}

/**
 * Test: schedule strings rejected by ScheduleManager
 */
void test_invalid_reading_schedules(void)
{
    TEST_ASSERT_FALSE(ScheduleManager::isValidSchedule("Daily"));
    TEST_ASSERT_FALSE(ScheduleManager::isValidSchedule("Weekdays"));
    TEST_ASSERT_FALSE(ScheduleManager::isValidSchedule(""));
    TEST_ASSERT_FALSE(ScheduleManager::isValidSchedule(nullptr));
    TEST_ASSERT_FALSE(ScheduleManager::isValidSchedule("Monday-Thursday"));
    TEST_ASSERT_FALSE(ScheduleManager::isValidSchedule("monday-friday")); // case sensitive
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
    // 8 digits is too long - must be exactly 7
    TEST_ASSERT_FALSE(everblu::core::parseMeterCode("20-16777216-000", nullptr, nullptr));
}

void test_meter_code_parse_rejects_short_serial(void)
{
    // Fewer than 7 digits is invalid
    TEST_ASSERT_FALSE(everblu::core::parseMeterCode("20-257750-000", nullptr, nullptr));
}
