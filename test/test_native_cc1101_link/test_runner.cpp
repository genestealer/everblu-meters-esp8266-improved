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

    return UNITY_END();
}
