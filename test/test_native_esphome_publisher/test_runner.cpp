/**
 * @file test_runner.cpp
 * @brief Unity entry point for the ESPHome publisher host suite
 */

#include <unity.h>

void esphomePublisherSetUp();
void esphomePublisherTearDown();

void test_pub_reading_populates_every_sensor(void);
void test_pub_reading_publishes_each_sensor_once(void);
void test_pub_reading_formats_wake_window_with_leading_zeros(void);
void test_pub_reading_omits_empty_meter_clock_and_model(void);
void test_pub_reading_publishes_meter_clock_and_model_when_present(void);
void test_pub_reading_publishes_frequency_estimate_in_khz(void);
void test_pub_reading_without_sensors_is_safe(void);
void test_pub_lqi_percentage_matches_the_shared_helper(void);
void test_pub_lqi_percentage_masks_the_crc_ok_bit(void);
void test_pub_rssi_percentage_matches_the_shared_helper(void);
void test_pub_rssi_percentage_is_clamped_to_0_100(void);
void test_pub_history_publishes_json_payload(void);
void test_pub_history_reports_unavailable_when_not_decoded(void);
void test_pub_history_reports_unavailable_for_a_null_array(void);
void test_pub_history_without_a_sensor_is_safe(void);
void test_pub_radio_state_drives_the_connected_binary_sensor(void);
void test_pub_radio_state_null_is_ignored(void);
void test_pub_status_and_error_are_published(void);
void test_pub_status_and_error_ignore_null(void);
void test_pub_active_reading_is_published(void);
void test_pub_statistics_are_published(void);
void test_pub_frequency_offset_is_published_in_khz(void);
void test_pub_tuned_frequency_is_published_in_mhz(void);
void test_pub_negative_frequency_estimate_is_signed(void);
void test_pub_meter_settings_are_formatted_for_display(void);
void test_pub_meter_settings_ignore_null_strings(void);
void test_pub_firmware_version_ignores_null(void);
void test_pub_uptime_is_published(void);
void test_pub_is_always_ready(void);
void test_pub_wifi_details_and_discovery_are_no_ops(void);
void test_pub_shared_sensors_keep_their_first_registration(void);
void test_pub_per_meter_sensors_are_not_shared(void);

void setUp(void) { esphomePublisherSetUp(); }
void tearDown(void) { esphomePublisherTearDown(); }

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_pub_reading_populates_every_sensor);
    RUN_TEST(test_pub_reading_publishes_each_sensor_once);
    RUN_TEST(test_pub_reading_formats_wake_window_with_leading_zeros);
    RUN_TEST(test_pub_reading_omits_empty_meter_clock_and_model);
    RUN_TEST(test_pub_reading_publishes_meter_clock_and_model_when_present);
    RUN_TEST(test_pub_reading_publishes_frequency_estimate_in_khz);
    RUN_TEST(test_pub_reading_without_sensors_is_safe);

    RUN_TEST(test_pub_lqi_percentage_matches_the_shared_helper);
    RUN_TEST(test_pub_lqi_percentage_masks_the_crc_ok_bit);
    RUN_TEST(test_pub_rssi_percentage_matches_the_shared_helper);
    RUN_TEST(test_pub_rssi_percentage_is_clamped_to_0_100);

    RUN_TEST(test_pub_history_publishes_json_payload);
    RUN_TEST(test_pub_history_reports_unavailable_when_not_decoded);
    RUN_TEST(test_pub_history_reports_unavailable_for_a_null_array);
    RUN_TEST(test_pub_history_without_a_sensor_is_safe);

    RUN_TEST(test_pub_radio_state_drives_the_connected_binary_sensor);
    RUN_TEST(test_pub_radio_state_null_is_ignored);
    RUN_TEST(test_pub_status_and_error_are_published);
    RUN_TEST(test_pub_status_and_error_ignore_null);
    RUN_TEST(test_pub_active_reading_is_published);

    RUN_TEST(test_pub_statistics_are_published);
    RUN_TEST(test_pub_frequency_offset_is_published_in_khz);
    RUN_TEST(test_pub_tuned_frequency_is_published_in_mhz);
    RUN_TEST(test_pub_negative_frequency_estimate_is_signed);
    RUN_TEST(test_pub_meter_settings_are_formatted_for_display);
    RUN_TEST(test_pub_meter_settings_ignore_null_strings);
    RUN_TEST(test_pub_firmware_version_ignores_null);
    RUN_TEST(test_pub_uptime_is_published);
    RUN_TEST(test_pub_is_always_ready);
    RUN_TEST(test_pub_wifi_details_and_discovery_are_no_ops);

    RUN_TEST(test_pub_shared_sensors_keep_their_first_registration);
    RUN_TEST(test_pub_per_meter_sensors_are_not_shared);

    return UNITY_END();
}
