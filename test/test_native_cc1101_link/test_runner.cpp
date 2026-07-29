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

void setUp(void)
{
    nativePinReset();
    nativeCC1101Install();
}

void tearDown(void) {}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_cc1101_init_succeeds_on_a_healthy_bus);
    RUN_TEST(test_cc1101_init_rejects_miso_stuck_at_0x0f);
    RUN_TEST(test_cc1101_init_rejects_miso_stuck_at_any_constant);
    RUN_TEST(test_cc1101_init_rejects_a_stuck_value_that_looks_like_a_real_version);
    RUN_TEST(test_cc1101_init_rejects_an_absent_radio);
    RUN_TEST(test_cc1101_init_accepts_an_unknown_silicon_revision);
    RUN_TEST(test_cc1101_init_tolerates_an_unstable_bus);
    RUN_TEST(test_cc1101_init_probes_both_bit_polarities);
    RUN_TEST(test_cc1101_init_re_runs_the_self_test_on_every_call);

    return UNITY_END();
}
