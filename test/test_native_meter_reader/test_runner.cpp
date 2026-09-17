/**
 * @file test_runner.cpp
 * @brief Unity entry point for the MeterReader / FrequencyManager host suite
 *
 * Unity allows exactly one setUp/tearDown/main per test binary, so every test
 * case in this folder is declared and registered here.
 */

#include <unity.h>

#include "native_fakes.h"

void meterReaderSetUp();
void frequencyManagerSetUp();

// test_meter_reader.cpp
void test_begin_reports_radio_failure(void);
void test_begin_tunes_radio_to_base_plus_stored_offset(void);
void test_begin_converts_utc_reading_time_to_local(void);
void test_successful_read_publishes_once_and_returns_to_idle(void);
void test_successful_read_passes_configured_meter_identity(void);
void test_successful_read_always_publishes_history_with_its_availability(void);
void test_reading_is_skipped_when_publisher_not_ready(void);
void test_failed_read_schedules_retry_after_delay(void);
void test_retry_sequence_keeps_active_reading_raised_until_it_ends(void);
void test_read_gives_up_after_max_retries(void);
void test_single_retry_configuration_fails_immediately(void);
void test_final_error_keeps_the_most_informative_failure(void);
void test_no_reply_failure_reports_the_no_response_message(void);
void test_success_after_failures_clears_the_error(void);
void test_zero_volume_reading_counts_as_a_failure(void);
void test_trigger_is_ignored_while_a_sequence_is_running(void);
void test_stop_cancels_a_pending_retry(void);
void test_stop_when_idle_does_not_publish_state(void);
void test_scheduled_read_triggers_once_at_the_configured_time(void);
void test_scheduled_read_fires_when_sampled_mid_minute(void);
void test_scheduled_read_clamps_out_of_range_hour(void);
void test_scheduled_read_is_skipped_on_a_non_reading_day(void);
void test_reading_day_gate_covers_every_schedule_string(void);
void test_scheduled_read_fires_again_on_the_same_day_of_year_next_year(void);
void test_scheduled_read_survives_a_scan_holding_the_radio(void);
void test_scheduled_read_survives_a_scan_that_outlasts_the_minute(void);
void test_scheduled_read_survives_a_cooldown_that_outlasts_the_minute(void);
void test_scheduled_read_is_not_owed_after_the_day_rolls_over(void);
void test_scheduled_read_waits_for_time_sync(void);
void test_disabled_scheduled_readings_block_the_daily_read(void);
void test_disabled_scheduled_readings_still_allow_manual_reads(void);
void test_cooldown_blocks_scheduled_reads_until_it_expires(void);
void test_cooldown_applies_to_a_failure_at_time_zero(void);
void test_statistics_are_republished_periodically(void);
void test_auto_scan_on_failure_runs_once_per_failure_streak(void);
void test_auto_scan_on_failure_is_rearmed_by_a_success(void);
void test_staged_scan_falls_back_and_finds_a_narrow_carrier();
void test_staged_scan_refines_on_both_sides_of_the_first_response();
void test_staged_scan_cost_does_not_scale_with_the_response_band();
void test_calibration_profiles_keep_independent_storage_and_tracking();
void test_staged_scan_cancels_every_phase_without_saving();
void test_staged_scan_radio_faults_never_save_candidates();
void test_staged_scan_requires_verification_without_prior_calibration();
void test_staged_scan_prefers_reliable_decodes_over_lower_error();
void test_staged_scan_carries_on_through_a_quiet_spell();
void test_auto_scan_on_failure_escalates_to_a_full_sweep(void);
void test_auto_scan_on_failure_does_not_escalate_after_a_cancel(void);
void test_auto_scan_on_failure_stays_off_when_disabled(void);
void test_a_scan_that_stores_a_new_offset_takes_one_confirmation_read(void);
void test_a_scan_that_keeps_the_existing_offset_takes_no_extra_read(void);
void test_a_failed_confirmation_read_ends_the_streak_without_rescanning(void);
void test_a_scan_is_only_stepped_by_the_reader_that_started_it(void);
void test_a_running_scan_blocks_reads_and_further_scan_requests(void);
void test_a_gas_meter_scans_the_same_as_a_water_meter(void);
void test_reset_frequency_offset_clears_storage_and_retunes(void);
void test_reset_frequency_offset_rearms_auto_scan(void);
void test_reset_frequency_offset_reports_storage_failure(void);
void test_successful_reads_feed_adaptive_frequency_tracking(void);
void test_small_frequency_errors_do_not_move_the_offset(void);
void test_reset_frequency_offset_reports_radio_failure(void);
void test_history_available_but_all_zero_is_not_treated_as_valid(void);
void test_read_without_history_still_publishes_to_clear_the_sensor(void);
void test_misconfigured_gas_volume_divisor_falls_back_without_failing_the_read(void);
void test_negative_timezone_offset_wraps_reading_time_to_previous_day(void);
void test_stop_reading_cancels_a_scan_with_no_read_in_progress(void);
void test_boot_scan_runs_once_when_the_meter_has_no_stored_calibration(void);
void test_boot_scan_is_skipped_when_a_calibration_is_already_stored(void);
void test_a_recovery_scan_stays_local_and_reports_itself_as_such(void);
void test_a_radio_fault_fails_the_read_before_the_meter_is_contacted(void);

// test_frequency_manager.cpp
void test_freq_begin_without_callbacks_is_refused(void);
void test_freq_begin_starts_uncalibrated_when_storage_is_empty(void);
void test_freq_offset_survives_a_reboot(void);
void test_freq_offset_outside_the_valid_range_is_discarded(void);
void test_freq_offset_with_a_wrong_magic_is_discarded(void);
void test_freq_auto_scan_is_requested_only_while_uncalibrated(void);
void test_freq_stored_offset_is_readable_without_reloading_the_manager(void);
void test_freq_releasing_the_active_calibration_ends_its_scan(void);
void test_freq_scan_that_cannot_persist_its_result_is_abandoned(void);
void test_freq_scan_finds_a_carrier_above_the_base_frequency(void);
void test_freq_scan_finds_a_carrier_below_the_base_frequency(void);
void test_freq_scan_persists_its_result(void);
void test_freq_scan_leaves_the_radio_tuned_to_the_result(void);
void test_freq_scan_without_a_carrier_keeps_the_base_frequency(void);
void test_freq_scan_aborts_when_the_radio_stops_responding(void);
void test_freq_scan_can_be_cancelled_and_restores_the_known_good_tuning(void);
void test_freq_scan_keeps_a_good_stored_offset_when_the_candidate_is_worse(void);
void test_freq_scan_replaces_a_stored_offset_that_no_longer_decodes(void);
void test_freq_scan_narrow_range_visits_fewer_steps_than_a_deep_sweep(void);
void test_freq_scan_advances_one_step_per_loop_and_stops_between_steps(void);
void test_freq_scan_ignores_a_second_start_while_one_is_running(void);
void test_freq_scan_refuses_to_start_without_callbacks(void);
void test_freq_loop_scan_does_nothing_when_no_scan_is_running(void);
void test_freq_scan_reports_its_result_through_the_status_callback(void);
void test_freq_scan_reports_a_failed_sweep_through_the_status_callback(void);
void test_freq_scan_keeps_the_stored_offset_when_the_candidate_stops_answering(void);
void test_freq_scan_saves_nothing_when_the_refinement_cannot_retune(void);
void test_freq_adaptive_tracking_waits_for_the_threshold(void);
void test_freq_adaptive_tracking_applies_half_the_average_error(void);
void test_freq_adaptive_tracking_cancels_symmetric_noise(void);
void test_freq_adaptive_tracking_corrects_downwards(void);
void test_freq_adaptive_tracking_retunes_and_saves_after_adjusting(void);
void test_freq_reset_adaptive_tracking_discards_the_accumulator(void);
void test_freq_scan_result_is_a_plain_tmeter_data_by_value(void);

void setUp(void)
{
    // Both files share the same baseline: every fake and all of the static
    // state inside FrequencyManager is reset before each case.
    meterReaderSetUp();
    frequencyManagerSetUp();
}

void tearDown(void) {}

int main(int, char **)
{
    UNITY_BEGIN();

    // MeterReader
    RUN_TEST(test_begin_reports_radio_failure);
    RUN_TEST(test_begin_tunes_radio_to_base_plus_stored_offset);
    RUN_TEST(test_begin_converts_utc_reading_time_to_local);
    RUN_TEST(test_successful_read_publishes_once_and_returns_to_idle);
    RUN_TEST(test_successful_read_passes_configured_meter_identity);
    RUN_TEST(test_successful_read_always_publishes_history_with_its_availability);
    RUN_TEST(test_reading_is_skipped_when_publisher_not_ready);
    RUN_TEST(test_failed_read_schedules_retry_after_delay);
    RUN_TEST(test_retry_sequence_keeps_active_reading_raised_until_it_ends);
    RUN_TEST(test_read_gives_up_after_max_retries);
    RUN_TEST(test_single_retry_configuration_fails_immediately);
    RUN_TEST(test_final_error_keeps_the_most_informative_failure);
    RUN_TEST(test_no_reply_failure_reports_the_no_response_message);
    RUN_TEST(test_success_after_failures_clears_the_error);
    RUN_TEST(test_zero_volume_reading_counts_as_a_failure);
    RUN_TEST(test_trigger_is_ignored_while_a_sequence_is_running);
    RUN_TEST(test_stop_cancels_a_pending_retry);
    RUN_TEST(test_stop_when_idle_does_not_publish_state);
    RUN_TEST(test_scheduled_read_triggers_once_at_the_configured_time);
    RUN_TEST(test_scheduled_read_fires_when_sampled_mid_minute);
    RUN_TEST(test_scheduled_read_clamps_out_of_range_hour);
    RUN_TEST(test_scheduled_read_is_skipped_on_a_non_reading_day);
    RUN_TEST(test_reading_day_gate_covers_every_schedule_string);
    RUN_TEST(test_scheduled_read_fires_again_on_the_same_day_of_year_next_year);
    RUN_TEST(test_scheduled_read_survives_a_scan_holding_the_radio);
    RUN_TEST(test_scheduled_read_survives_a_scan_that_outlasts_the_minute);
    RUN_TEST(test_scheduled_read_survives_a_cooldown_that_outlasts_the_minute);
    RUN_TEST(test_scheduled_read_is_not_owed_after_the_day_rolls_over);
    RUN_TEST(test_scheduled_read_waits_for_time_sync);
    RUN_TEST(test_disabled_scheduled_readings_block_the_daily_read);
    RUN_TEST(test_disabled_scheduled_readings_still_allow_manual_reads);
    RUN_TEST(test_cooldown_blocks_scheduled_reads_until_it_expires);
    RUN_TEST(test_cooldown_applies_to_a_failure_at_time_zero);
    RUN_TEST(test_statistics_are_republished_periodically);
    RUN_TEST(test_auto_scan_on_failure_runs_once_per_failure_streak);
    RUN_TEST(test_auto_scan_on_failure_is_rearmed_by_a_success);
    RUN_TEST(test_auto_scan_on_failure_escalates_to_a_full_sweep);
    RUN_TEST(test_auto_scan_on_failure_does_not_escalate_after_a_cancel);
    RUN_TEST(test_auto_scan_on_failure_stays_off_when_disabled);
    RUN_TEST(test_a_scan_that_stores_a_new_offset_takes_one_confirmation_read);
    RUN_TEST(test_a_scan_that_keeps_the_existing_offset_takes_no_extra_read);
    RUN_TEST(test_a_failed_confirmation_read_ends_the_streak_without_rescanning);
    RUN_TEST(test_a_scan_is_only_stepped_by_the_reader_that_started_it);
    RUN_TEST(test_a_running_scan_blocks_reads_and_further_scan_requests);
    RUN_TEST(test_a_gas_meter_scans_the_same_as_a_water_meter);
    RUN_TEST(test_reset_frequency_offset_clears_storage_and_retunes);
    RUN_TEST(test_reset_frequency_offset_rearms_auto_scan);
    RUN_TEST(test_reset_frequency_offset_reports_storage_failure);
    RUN_TEST(test_successful_reads_feed_adaptive_frequency_tracking);
    RUN_TEST(test_small_frequency_errors_do_not_move_the_offset);
    RUN_TEST(test_reset_frequency_offset_reports_radio_failure);
    RUN_TEST(test_history_available_but_all_zero_is_not_treated_as_valid);
    RUN_TEST(test_read_without_history_still_publishes_to_clear_the_sensor);
    RUN_TEST(test_misconfigured_gas_volume_divisor_falls_back_without_failing_the_read);
    RUN_TEST(test_negative_timezone_offset_wraps_reading_time_to_previous_day);
    RUN_TEST(test_stop_reading_cancels_a_scan_with_no_read_in_progress);
    RUN_TEST(test_boot_scan_runs_once_when_the_meter_has_no_stored_calibration);
    RUN_TEST(test_boot_scan_is_skipped_when_a_calibration_is_already_stored);
    RUN_TEST(test_a_recovery_scan_stays_local_and_reports_itself_as_such);
    RUN_TEST(test_a_radio_fault_fails_the_read_before_the_meter_is_contacted);

    // FrequencyManager
    RUN_TEST(test_freq_begin_without_callbacks_is_refused);
    RUN_TEST(test_freq_begin_starts_uncalibrated_when_storage_is_empty);
    RUN_TEST(test_freq_offset_survives_a_reboot);
    RUN_TEST(test_freq_offset_outside_the_valid_range_is_discarded);
    RUN_TEST(test_freq_offset_with_a_wrong_magic_is_discarded);
    RUN_TEST(test_freq_auto_scan_is_requested_only_while_uncalibrated);
    RUN_TEST(test_freq_stored_offset_is_readable_without_reloading_the_manager);
    RUN_TEST(test_freq_releasing_the_active_calibration_ends_its_scan);
    RUN_TEST(test_freq_scan_that_cannot_persist_its_result_is_abandoned);
    RUN_TEST(test_freq_scan_finds_a_carrier_above_the_base_frequency);
    RUN_TEST(test_freq_scan_finds_a_carrier_below_the_base_frequency);
    RUN_TEST(test_freq_scan_persists_its_result);
    RUN_TEST(test_freq_scan_leaves_the_radio_tuned_to_the_result);
    RUN_TEST(test_freq_scan_without_a_carrier_keeps_the_base_frequency);
    RUN_TEST(test_freq_scan_aborts_when_the_radio_stops_responding);
    RUN_TEST(test_freq_scan_can_be_cancelled_and_restores_the_known_good_tuning);
    RUN_TEST(test_freq_scan_keeps_a_good_stored_offset_when_the_candidate_is_worse);
    RUN_TEST(test_freq_scan_replaces_a_stored_offset_that_no_longer_decodes);
    RUN_TEST(test_freq_scan_narrow_range_visits_fewer_steps_than_a_deep_sweep);
    RUN_TEST(test_freq_scan_advances_one_step_per_loop_and_stops_between_steps);
    RUN_TEST(test_freq_scan_ignores_a_second_start_while_one_is_running);
    RUN_TEST(test_freq_scan_refuses_to_start_without_callbacks);
    RUN_TEST(test_freq_loop_scan_does_nothing_when_no_scan_is_running);
    RUN_TEST(test_freq_scan_reports_its_result_through_the_status_callback);
    RUN_TEST(test_freq_scan_reports_a_failed_sweep_through_the_status_callback);
    RUN_TEST(test_freq_scan_keeps_the_stored_offset_when_the_candidate_stops_answering);
    RUN_TEST(test_freq_scan_saves_nothing_when_the_refinement_cannot_retune);
    RUN_TEST(test_freq_adaptive_tracking_waits_for_the_threshold);
    RUN_TEST(test_freq_adaptive_tracking_applies_half_the_average_error);
    RUN_TEST(test_freq_adaptive_tracking_cancels_symmetric_noise);
    RUN_TEST(test_freq_adaptive_tracking_corrects_downwards);
    RUN_TEST(test_freq_adaptive_tracking_retunes_and_saves_after_adjusting);
    RUN_TEST(test_freq_reset_adaptive_tracking_discards_the_accumulator);
    RUN_TEST(test_freq_scan_result_is_a_plain_tmeter_data_by_value);

    RUN_TEST(test_staged_scan_falls_back_and_finds_a_narrow_carrier);
    RUN_TEST(test_staged_scan_refines_on_both_sides_of_the_first_response);
    RUN_TEST(test_staged_scan_cost_does_not_scale_with_the_response_band);
    RUN_TEST(test_calibration_profiles_keep_independent_storage_and_tracking);
    RUN_TEST(test_staged_scan_cancels_every_phase_without_saving);
    RUN_TEST(test_staged_scan_radio_faults_never_save_candidates);
    RUN_TEST(test_staged_scan_requires_verification_without_prior_calibration);
    RUN_TEST(test_staged_scan_prefers_reliable_decodes_over_lower_error);
    RUN_TEST(test_staged_scan_carries_on_through_a_quiet_spell);

    return UNITY_END();
}
