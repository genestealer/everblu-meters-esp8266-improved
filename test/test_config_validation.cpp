/**
 * @file test_config_validation.cpp
 * @brief Unit tests for configuration validation functions
 */

#include <unity.h>
#include <Arduino.h>

// Mock configuration values for testing
#define TEST_METER_YEAR 2020
#define TEST_METER_SERIAL 12345678
#define TEST_FREQUENCY 433.82

// Forward declarations of functions under test
extern bool isValidReadingSchedule(const char* schedule);
extern bool validateConfiguration();

void setUp(void) {
    // This runs before each test
}

void tearDown(void) {
    // This runs after each test
}

/**
 * Test: isValidReadingSchedule with valid inputs
 */
void test_valid_reading_schedules(void) {
    TEST_ASSERT_TRUE(isValidReadingSchedule("Monday-Friday"));
    TEST_ASSERT_TRUE(isValidReadingSchedule("Monday-Saturday"));
    TEST_ASSERT_TRUE(isValidReadingSchedule("Monday-Sunday"));
}

/**
 * Test: isValidReadingSchedule with invalid inputs
 */
void test_invalid_reading_schedules(void) {
    TEST_ASSERT_FALSE(isValidReadingSchedule("Daily"));
    TEST_ASSERT_FALSE(isValidReadingSchedule("Weekdays"));
    TEST_ASSERT_FALSE(isValidReadingSchedule(""));
    TEST_ASSERT_FALSE(isValidReadingSchedule(NULL));
    TEST_ASSERT_FALSE(isValidReadingSchedule("Monday-Thursday"));
}

/**
 * Test: Meter year validation
 */
void test_meter_year_validation(void) {
    // Valid years (2009 onwards when RADIAN was introduced)
    TEST_ASSERT_TRUE(TEST_METER_YEAR >= 2009);
    TEST_ASSERT_TRUE(TEST_METER_YEAR <= 2025);
}

/**
 * Test: Meter serial validation
 */
void test_meter_serial_validation(void) {
    // Valid serial (non-zero, max 8 digits)
    TEST_ASSERT_TRUE(TEST_METER_SERIAL > 0);
    TEST_ASSERT_TRUE(TEST_METER_SERIAL <= 99999999);
}

/**
 * Test: Frequency validation
 */
void test_frequency_validation(void) {
    // Valid frequency (433 MHz ±1 MHz)
    TEST_ASSERT_FLOAT_WITHIN(1.0, 433.82, TEST_FREQUENCY);
    TEST_ASSERT_TRUE(TEST_FREQUENCY >= 300.0);
    TEST_ASSERT_TRUE(TEST_FREQUENCY <= 500.0);
}

void setup() {
    delay(2000); // Wait for serial monitor

    UNITY_BEGIN();
    
    RUN_TEST(test_valid_reading_schedules);
    RUN_TEST(test_invalid_reading_schedules);
    RUN_TEST(test_meter_year_validation);
    RUN_TEST(test_meter_serial_validation);
    RUN_TEST(test_frequency_validation);
    
    UNITY_END();
}

void loop() {
    // Nothing to do here
}
