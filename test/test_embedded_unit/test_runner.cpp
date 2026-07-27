/**
 * @file test_runner.cpp
 * @brief Single Unity entry point for the test_embedded_unit suite
 *
 * The suite is split across several translation units (config validation,
 * schedule manager, utils). Unity requires exactly one `setUp`/`tearDown` pair
 * and one `main`, so both live here and every test case is registered below.
 *
 * This suite runs on the host via `pio test -e native`.
 */

#include <unity.h>

// --- test_config_validation.cpp ---
void test_valid_reading_schedules(void);
void test_invalid_reading_schedules(void);
void test_frequency_validation(void);
void test_meter_code_parse_valid_dashed_with_suffix(void);
void test_meter_code_parse_valid_dashed_without_suffix(void);
void test_meter_code_parse_rejects_non_digit(void);
void test_meter_code_parse_rejects_missing_dash_format(void);
void test_meter_code_parse_rejects_zero_serial(void);
void test_meter_code_parse_rejects_serial_over_24bit(void);
void test_meter_code_parse_rejects_short_serial(void);

// --- test_schedule_manager.cpp ---
void test_schedule_monday_friday(void);
void test_schedule_monday_saturday(void);
void test_schedule_monday_sunday_includes_sunday(void);
void test_schedule_monday_only(void);
void test_schedule_tuesday_only(void);
void test_schedule_wednesday_only(void);
void test_schedule_thursday_only(void);
void test_schedule_friday_only(void);
void test_schedule_saturday_only(void);
void test_schedule_sunday_only(void);
void test_schedule_invalid(void);
void test_schedule_empty(void);
void test_schedule_null(void);
void test_all_schedules_all_days(void);
void test_schedule_null_tm_is_not_a_reading_day(void);
void test_reading_time_utc_to_local_positive_offset(void);
void test_reading_time_utc_to_local_negative_offset(void);
void test_reading_time_local_to_utc_roundtrip(void);
void test_reading_time_is_clamped(void);
void test_auto_align_uses_window_midpoint(void);
void test_auto_align_rejects_zero_length_window(void);

// --- test_utils.cpp ---
void test_crc_known_data(void);
void test_crc_empty_data(void);
void test_crc_different_data(void);
void test_crc_deterministic(void);
void test_crc_detects_single_bit_flip(void);
void test_crc_is_order_sensitive(void);

void setUp(void) {}

void tearDown(void) {}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_valid_reading_schedules);
    RUN_TEST(test_invalid_reading_schedules);
    RUN_TEST(test_frequency_validation);
    RUN_TEST(test_meter_code_parse_valid_dashed_with_suffix);
    RUN_TEST(test_meter_code_parse_valid_dashed_without_suffix);
    RUN_TEST(test_meter_code_parse_rejects_non_digit);
    RUN_TEST(test_meter_code_parse_rejects_missing_dash_format);
    RUN_TEST(test_meter_code_parse_rejects_zero_serial);
    RUN_TEST(test_meter_code_parse_rejects_serial_over_24bit);
    RUN_TEST(test_meter_code_parse_rejects_short_serial);

    RUN_TEST(test_schedule_monday_friday);
    RUN_TEST(test_schedule_monday_saturday);
    RUN_TEST(test_schedule_monday_sunday_includes_sunday);
    RUN_TEST(test_schedule_monday_only);
    RUN_TEST(test_schedule_tuesday_only);
    RUN_TEST(test_schedule_wednesday_only);
    RUN_TEST(test_schedule_thursday_only);
    RUN_TEST(test_schedule_friday_only);
    RUN_TEST(test_schedule_saturday_only);
    RUN_TEST(test_schedule_sunday_only);
    RUN_TEST(test_schedule_invalid);
    RUN_TEST(test_schedule_empty);
    RUN_TEST(test_schedule_null);
    RUN_TEST(test_all_schedules_all_days);
    RUN_TEST(test_schedule_null_tm_is_not_a_reading_day);
    RUN_TEST(test_reading_time_utc_to_local_positive_offset);
    RUN_TEST(test_reading_time_utc_to_local_negative_offset);
    RUN_TEST(test_reading_time_local_to_utc_roundtrip);
    RUN_TEST(test_reading_time_is_clamped);
    RUN_TEST(test_auto_align_uses_window_midpoint);
    RUN_TEST(test_auto_align_rejects_zero_length_window);

    RUN_TEST(test_crc_known_data);
    RUN_TEST(test_crc_empty_data);
    RUN_TEST(test_crc_different_data);
    RUN_TEST(test_crc_deterministic);
    RUN_TEST(test_crc_detects_single_bit_flip);
    RUN_TEST(test_crc_is_order_sensitive);

    return UNITY_END();
}
