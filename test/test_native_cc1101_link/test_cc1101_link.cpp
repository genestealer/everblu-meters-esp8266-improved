/**
 * @file test_cc1101_link.cpp
 * @brief Boot-time SPI link self-test in cc1101_init()
 *
 * These run the real `src/core/cc1101.cpp` against a simulated CC1101 on a
 * simulated SPI bus, so the write/read-back probe itself is under test rather
 * than a re-implementation of it.
 *
 * Motivation (issue #136): a board whose MISO line was stuck returned the same
 * byte for every register. The old check only rejected VERSION 0x00 and 0xFF, so
 * a line stuck at 0x0F sailed through and the firmware reported "Radio found
 * OK". Every later read - RSSI, LQI, MARCSTATE, the whole RX FIFO - was that one
 * constant, and the fault surfaced hundreds of lines later as an unexplained CRC
 * failure that looked like a weak radio link.
 */

#include <unity.h>

#include "native_cc1101_device.h"
#include "core/cc1101.h"

static constexpr float kTestFrequency = 433.82f;

/** SYNC1/SYNC0, the scratch pair the probe writes through. */
static constexpr uint8_t kSync1Register = 0x04;
static constexpr uint8_t kSync0Register = 0x05;

void test_cc1101_init_succeeds_on_a_healthy_bus(void)
{
    nativeCC1101Install();

    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));
    TEST_ASSERT_GREATER_THAN_UINT32(0, nativeCC1101().resetStrobes);
}

void test_cc1101_init_rejects_miso_stuck_at_0x0f(void)
{
    // The exact failure reported from a LilyGO T-Embed CC1101 Plus: every
    // register, the status byte and the whole RX FIFO read back as 0x0F.
    nativeCC1101Install();
    nativeCC1101().fault = NativeSpiFault::StuckConstant;
    nativeCC1101().stuckValue = 0x0F;

    TEST_ASSERT_FALSE(cc1101_init(kTestFrequency));
}

void test_cc1101_init_rejects_miso_stuck_at_any_constant(void)
{
    // 0x00 and 0xFF were the only values the old check caught. Nothing about a
    // stuck line makes those two special, so every constant must be rejected.
    const uint8_t stuck_values[] = {0x00, 0x01, 0x0F, 0x14, 0x55, 0xAA, 0xFF};

    for (uint8_t stuck : stuck_values)
    {
        nativeCC1101Install();
        nativeCC1101().fault = NativeSpiFault::StuckConstant;
        nativeCC1101().stuckValue = stuck;

        char message[64];
        snprintf(message, sizeof(message), "MISO stuck at 0x%02X was accepted", stuck);
        TEST_ASSERT_FALSE_MESSAGE(cc1101_init(kTestFrequency), message);
    }
}

void test_cc1101_init_rejects_a_stuck_value_that_looks_like_a_real_version(void)
{
    // 0x14 is a genuine CC1101 silicon revision, so an identity check alone
    // would be fooled by a line stuck at that value. The write/read-back probe
    // is what makes the verdict independent of the value observed.
    nativeCC1101Install();
    nativeCC1101().fault = NativeSpiFault::StuckConstant;
    nativeCC1101().stuckValue = kCc1101Version;

    TEST_ASSERT_FALSE(cc1101_init(kTestFrequency));
}

void test_cc1101_init_rejects_an_absent_radio(void)
{
    // Nothing on the bus at all: no handler, so MISO floats high.
    nativeCC1101().reset();
    nativeSpiSetHandler(nullptr);

    TEST_ASSERT_FALSE(cc1101_init(kTestFrequency));

    nativeCC1101Install(); // leave the bus usable for the next test
}

void test_cc1101_init_accepts_an_unknown_silicon_revision(void)
{
    // Clones report revisions we do not recognise. Once the read-back probe has
    // passed we know we are really talking to something, so an unknown VERSION
    // is a warning rather than a refusal to start.
    nativeCC1101Install();
    nativeCC1101().version = 0x07;

    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));
}

void test_cc1101_init_tolerates_an_unstable_bus(void)
{
    // A bus that answers correctly but inconsistently is worth warning about,
    // not worth refusing to start on: the radio may still be usable, and a hard
    // failure here would strand a device that was merely clocked too fast.
    nativeCC1101Install();
    nativeCC1101().fault = NativeSpiFault::UnstableStatus;

    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));
    TEST_ASSERT_GREATER_THAN_UINT32(1, nativeCC1101().statusReads);
}

void test_cc1101_init_probes_both_bit_polarities(void)
{
    // The probe must drive every data bit both high and low, otherwise a line
    // shorted high or low could still follow one of the two patterns. Checking
    // the scratch registers moved proves both writes reached the device.
    nativeCC1101Install();
    nativeCC1101().config[kSync1Register] = 0x00;
    nativeCC1101().config[kSync0Register] = 0x00;

    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));

    // cc1101_configureRF_0() restores the real RADIAN sync word afterwards, so
    // the scratch values must not have been left behind.
    TEST_ASSERT_EQUAL_HEX8(0x55, nativeCC1101().config[kSync1Register]);
    TEST_ASSERT_EQUAL_HEX8(0x00, nativeCC1101().config[kSync0Register]);
}

void test_cc1101_init_re_runs_the_self_test_on_every_call(void)
{
    // Frequency scans call cc1101_init() repeatedly. A bus that dies partway
    // through a sweep must be caught then, not assumed good from boot.
    nativeCC1101Install();
    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));

    nativeCC1101().fault = NativeSpiFault::StuckConstant;
    nativeCC1101().stuckValue = 0x0F;
    TEST_ASSERT_FALSE(cc1101_init(kTestFrequency));

    nativeCC1101().fault = NativeSpiFault::None;
    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));
}
