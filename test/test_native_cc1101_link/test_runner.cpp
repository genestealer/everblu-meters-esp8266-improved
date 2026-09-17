/**
 * @file test_runner.cpp
 * @brief Unity entry point for the CC1101 SPI link host suite
 */

#include <unity.h>

#include "native_cc1101_device.h"

void test_cc1101_init_succeeds_on_a_healthy_bus(void);
void test_cc1101_init_rejects_miso_stuck_at_0x0f(void);
void test_cc1101_init_rejects_miso_stuck_at_any_constant(void);
void test_cc1101_init_rejects_a_stuck_value_that_looks_like_a_real_version(void);
void test_cc1101_init_rejects_an_absent_radio(void);
void test_cc1101_init_accepts_an_unknown_silicon_revision(void);
void test_cc1101_init_tolerates_an_unstable_bus(void);
void test_cc1101_init_probes_both_bit_polarities(void);
void test_cc1101_init_re_runs_the_self_test_on_every_call(void);
void test_probe_reports_the_radio_identity_on_a_healthy_bus(void);
void test_probe_rejects_a_stuck_bus_and_reports_what_it_saw(void);
void test_probe_accepts_null_outputs(void);
void test_diagnostics_capture_the_configured_radio_state(void);
void test_diagnostics_flag_an_untrustworthy_bus(void);
void test_diagnostics_tolerate_a_null_destination(void);
void test_diagnostics_leave_the_sync_word_intact(void);
void test_marcstate_names_cover_the_states_that_get_reported(void);
void test_marcstate_names_cover_every_datasheet_state(void);
void test_marcstate_name_masks_off_the_unused_upper_bits(void);
void test_freq_registers_decode_to_the_tuned_carrier(void);
void test_diagnostics_report_where_the_radio_is_actually_tuned(void);
void test_gdo0_self_test_is_reported_as_not_run_before_the_first_init(void);
void test_gdo0_self_test_passes_when_the_line_is_wired(void);
void test_gdo0_self_test_detects_a_pin_pointed_at_nothing(void);
void test_gdo0_verdict_clears_once_the_line_is_fixed(void);
void test_diagnostics_park_the_radio_and_put_it_back_in_rx(void);
void test_diagnostics_leave_an_idle_radio_idle(void);
void test_report_states_the_configuration_it_was_given(void);
void test_report_distinguishes_a_gas_meter_and_an_uninitialised_reader(void);
void test_report_falls_back_to_the_pins_the_driver_is_using(void);
void test_report_works_without_any_context(void);
void test_report_says_the_register_values_are_meaningless_on_a_stuck_bus(void);
void test_report_calls_out_a_gdo0_line_that_is_not_wired(void);
void test_report_is_truncated_rather_than_overrunning_its_buffer(void);
void test_rssi_convert2dbm_matches_the_datasheet(void);
void test_rssi_convert2dbm_does_not_wrap_below_int8_min(void);

// test_cc1101_read.cpp
void test_read_decodes_a_captured_reply_into_a_reading(void);
void test_read_reports_the_link_quality_it_measured(void);
void test_read_transmits_the_wake_up_burst_then_the_interrogation(void);
void test_read_reports_no_reply_when_the_meter_stays_silent(void);
void test_read_reports_a_corrupted_frame_rather_than_a_reading(void);
void test_read_blames_near_field_saturation_when_the_signal_is_too_strong(void);
void test_read_gives_up_when_the_reply_stops_part_way_through(void);
void test_read_times_out_when_the_frame_start_never_arrives(void);
void test_get_meter_data_resolves_the_configured_meter_code(void);
void test_every_recorded_capture_replays_through_the_radio(void);
void test_receive_recovers_a_radio_that_ignores_the_first_rx_strobe(void);
void test_receive_gives_up_on_a_radio_that_never_enters_rx(void);

void setUp(void)
{
    nativePinReset();
    nativeCC1101Install();
}

void tearDown(void) {}

int main(int, char **)
{
    UNITY_BEGIN();

    // MUST STAY FIRST: the GDO0 self-test verdict is process-wide state, so this is the
    // only point at which "no init has run yet" can still be observed.
    RUN_TEST(test_gdo0_self_test_is_reported_as_not_run_before_the_first_init);
    RUN_TEST(test_cc1101_init_succeeds_on_a_healthy_bus);
    RUN_TEST(test_cc1101_init_rejects_miso_stuck_at_0x0f);
    RUN_TEST(test_cc1101_init_rejects_miso_stuck_at_any_constant);
    RUN_TEST(test_cc1101_init_rejects_a_stuck_value_that_looks_like_a_real_version);
    RUN_TEST(test_cc1101_init_rejects_an_absent_radio);
    RUN_TEST(test_cc1101_init_accepts_an_unknown_silicon_revision);
    RUN_TEST(test_cc1101_init_tolerates_an_unstable_bus);
    RUN_TEST(test_cc1101_init_probes_both_bit_polarities);
    RUN_TEST(test_cc1101_init_re_runs_the_self_test_on_every_call);
    RUN_TEST(test_probe_reports_the_radio_identity_on_a_healthy_bus);
    RUN_TEST(test_probe_rejects_a_stuck_bus_and_reports_what_it_saw);
    RUN_TEST(test_probe_accepts_null_outputs);
    RUN_TEST(test_diagnostics_capture_the_configured_radio_state);
    RUN_TEST(test_diagnostics_flag_an_untrustworthy_bus);
    RUN_TEST(test_diagnostics_tolerate_a_null_destination);
    RUN_TEST(test_diagnostics_leave_the_sync_word_intact);
    RUN_TEST(test_marcstate_names_cover_the_states_that_get_reported);
    RUN_TEST(test_marcstate_names_cover_every_datasheet_state);
    RUN_TEST(test_marcstate_name_masks_off_the_unused_upper_bits);
    RUN_TEST(test_freq_registers_decode_to_the_tuned_carrier);
    RUN_TEST(test_diagnostics_report_where_the_radio_is_actually_tuned);
    RUN_TEST(test_gdo0_self_test_passes_when_the_line_is_wired);
    RUN_TEST(test_gdo0_self_test_detects_a_pin_pointed_at_nothing);
    RUN_TEST(test_gdo0_verdict_clears_once_the_line_is_fixed);
    RUN_TEST(test_diagnostics_park_the_radio_and_put_it_back_in_rx);
    RUN_TEST(test_diagnostics_leave_an_idle_radio_idle);
    RUN_TEST(test_report_states_the_configuration_it_was_given);
    RUN_TEST(test_report_distinguishes_a_gas_meter_and_an_uninitialised_reader);
    RUN_TEST(test_report_falls_back_to_the_pins_the_driver_is_using);
    RUN_TEST(test_report_works_without_any_context);
    RUN_TEST(test_report_says_the_register_values_are_meaningless_on_a_stuck_bus);
    RUN_TEST(test_report_calls_out_a_gdo0_line_that_is_not_wired);
    RUN_TEST(test_report_is_truncated_rather_than_overrunning_its_buffer);
    RUN_TEST(test_rssi_convert2dbm_matches_the_datasheet);
    RUN_TEST(test_rssi_convert2dbm_does_not_wrap_below_int8_min);

    RUN_TEST(test_read_decodes_a_captured_reply_into_a_reading);
    RUN_TEST(test_read_reports_the_link_quality_it_measured);
    RUN_TEST(test_read_transmits_the_wake_up_burst_then_the_interrogation);
    RUN_TEST(test_read_reports_no_reply_when_the_meter_stays_silent);
    RUN_TEST(test_read_reports_a_corrupted_frame_rather_than_a_reading);
    RUN_TEST(test_read_blames_near_field_saturation_when_the_signal_is_too_strong);
    RUN_TEST(test_read_gives_up_when_the_reply_stops_part_way_through);
    RUN_TEST(test_read_times_out_when_the_frame_start_never_arrives);
    RUN_TEST(test_get_meter_data_resolves_the_configured_meter_code);
    RUN_TEST(test_every_recorded_capture_replays_through_the_radio);
    RUN_TEST(test_receive_recovers_a_radio_that_ignores_the_first_rx_strobe);
    RUN_TEST(test_receive_gives_up_on_a_radio_that_never_enters_rx);

    return UNITY_END();
}
