/**
 * @file test_runner.cpp
 * @brief Unity entry point for the ESPHome-mode MeterReader host suite
 */

#include <unity.h>

void esphomeReaderSetUp();

void test_esphome_begin_does_not_publish_idle_states(void);
void test_esphome_begin_publishes_settings_and_calibration(void);
void test_esphome_begin_reports_a_radio_failure_immediately(void);
void test_esphome_begin_without_a_publisher_is_safe(void);
void test_esphome_scheduled_read_waits_for_home_assistant(void);
void test_esphome_losing_home_assistant_stops_scheduled_reads(void);
void test_esphome_manual_read_does_not_wait_for_home_assistant(void);
void test_esphome_successful_read_publishes_the_same_sequence(void);
void test_esphome_failed_read_reaches_the_cooldown(void);
void test_esphome_stop_reading_returns_to_idle(void);
void test_esphome_reset_frequency_offset_retunes_and_publishes(void);
void test_esphome_frequency_scan_publishes_the_new_offset(void);
void test_esphome_statistics_are_republished_periodically(void);

void setUp(void) { esphomeReaderSetUp(); }

void tearDown(void) {}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_esphome_begin_does_not_publish_idle_states);
    RUN_TEST(test_esphome_begin_publishes_settings_and_calibration);
    RUN_TEST(test_esphome_begin_reports_a_radio_failure_immediately);
    RUN_TEST(test_esphome_begin_without_a_publisher_is_safe);

    RUN_TEST(test_esphome_scheduled_read_waits_for_home_assistant);
    RUN_TEST(test_esphome_losing_home_assistant_stops_scheduled_reads);
    RUN_TEST(test_esphome_manual_read_does_not_wait_for_home_assistant);

    RUN_TEST(test_esphome_successful_read_publishes_the_same_sequence);
    RUN_TEST(test_esphome_failed_read_reaches_the_cooldown);
    RUN_TEST(test_esphome_stop_reading_returns_to_idle);
    RUN_TEST(test_esphome_reset_frequency_offset_retunes_and_publishes);
    RUN_TEST(test_esphome_frequency_scan_publishes_the_new_offset);
    RUN_TEST(test_esphome_statistics_are_republished_periodically);

    return UNITY_END();
}
