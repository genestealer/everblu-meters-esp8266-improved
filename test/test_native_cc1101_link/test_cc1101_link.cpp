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

// ---------------------------------------------------------------------------
// cc1101_probe_spi_link: the same check, callable before the radio is configured
// ---------------------------------------------------------------------------

void test_probe_reports_the_radio_identity_on_a_healthy_bus(void)
{
    // ESPHome calls this during setup() so a wiring fault lands in the boot log
    // that users actually capture, instead of waiting for the first read.
    nativeCC1101Install();

    uint8_t partnum = 0xEE;
    uint8_t version = 0xEE;
    TEST_ASSERT_TRUE(cc1101_probe_spi_link(&partnum, &version));
    TEST_ASSERT_EQUAL_HEX8(kCc1101PartNum, partnum);
    TEST_ASSERT_EQUAL_HEX8(kCc1101Version, version);
}

void test_probe_rejects_a_stuck_bus_and_reports_what_it_saw(void)
{
    // The caller shows these values to the user, so they must be the bytes that
    // were actually read - seeing PARTNUM and VERSION both equal to the stuck
    // value is what makes the fault recognisable.
    nativeCC1101Install();
    nativeCC1101().fault = NativeSpiFault::StuckConstant;
    nativeCC1101().stuckValue = 0x0F;

    uint8_t partnum = 0;
    uint8_t version = 0;
    TEST_ASSERT_FALSE(cc1101_probe_spi_link(&partnum, &version));
    TEST_ASSERT_EQUAL_HEX8(0x0F, partnum);
    TEST_ASSERT_EQUAL_HEX8(0x0F, version);
}

void test_probe_accepts_null_outputs(void)
{
    // The identity is optional: a caller that only wants the verdict should not
    // have to supply somewhere to put it.
    nativeCC1101Install();

    TEST_ASSERT_TRUE(cc1101_probe_spi_link(nullptr, nullptr));
}

// ---------------------------------------------------------------------------
// cc1101_collect_diagnostics: the one-shot snapshot behind the report button
// ---------------------------------------------------------------------------

void test_diagnostics_capture_the_configured_radio_state(void)
{
    nativeCC1101Install();
    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));

    cc1101_diagnostics_t diag;
    cc1101_collect_diagnostics(&diag);

    TEST_ASSERT_TRUE(diag.link_ok);
    TEST_ASSERT_EQUAL_HEX8(kCc1101PartNum, diag.partnum);
    TEST_ASSERT_EQUAL_HEX8(kCc1101Version, diag.version);
    // 433.82 MHz lands in the 0x10 page of the frequency word; the exact value
    // is asserted elsewhere, here it only has to be something the radio was
    // actually programmed with rather than a default or a stuck byte.
    TEST_ASSERT_EQUAL_HEX8(0x10, diag.freq2);
    TEST_ASSERT_EQUAL_HEX8(nativeCC1101().config[0x10], diag.mdmcfg4);
    TEST_ASSERT_EQUAL_HEX8(nativeCC1101().config[0x08], diag.pktctrl0);
}

void test_diagnostics_flag_an_untrustworthy_bus(void)
{
    // Every register reads back as the stuck value, so the report has to say the
    // numbers below it are meaningless rather than presenting them as readings.
    nativeCC1101Install();
    nativeCC1101().fault = NativeSpiFault::StuckConstant;
    nativeCC1101().stuckValue = 0x0F;

    cc1101_diagnostics_t diag;
    cc1101_collect_diagnostics(&diag);

    TEST_ASSERT_FALSE(diag.link_ok);
    TEST_ASSERT_EQUAL_HEX8(0x0F, diag.partnum);
    TEST_ASSERT_EQUAL_HEX8(0x0F, diag.version);
    TEST_ASSERT_EQUAL_HEX8(0x0F, diag.marcstate);
}

void test_diagnostics_tolerate_a_null_destination(void)
{
    cc1101_collect_diagnostics(nullptr); // must not crash
}

void test_diagnostics_leave_the_sync_word_intact(void)
{
    // The report button is pressed on a configured, listening radio. The link probe writes
    // its scratch patterns through SYNC1/SYNC0, and the last one it writes is 0x55/0xAA -
    // so without a restore the operational sync word would silently become 0x55AA and the
    // parked receiver would be matching on the wrong pattern.
    nativeCC1101Install();
    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));

    cc1101_diagnostics_t diag;
    cc1101_collect_diagnostics(&diag);

    TEST_ASSERT_EQUAL_HEX8(0x55, nativeCC1101().config[kSync1Register]);
    TEST_ASSERT_EQUAL_HEX8(0x00, nativeCC1101().config[kSync0Register]);
}

void test_marcstate_names_cover_the_states_that_get_reported(void)
{
    // These are the values users actually see in a report and mistake for faults.
    TEST_ASSERT_EQUAL_STRING("IDLE", cc1101_marcstate_name(0x01));
    TEST_ASSERT_EQUAL_STRING("RX", cc1101_marcstate_name(0x0D));
    TEST_ASSERT_EQUAL_STRING("RXFIFO_OVERFLOW", cc1101_marcstate_name(0x11));
    TEST_ASSERT_EQUAL_STRING("TXFIFO_UNDERFLOW", cc1101_marcstate_name(0x16));
    TEST_ASSERT_EQUAL_STRING("unknown", cc1101_marcstate_name(0x1F));
}

// ---------------------------------------------------------------------------
// GDO0 wiring self-test
// ---------------------------------------------------------------------------

void test_gdo0_self_test_passes_when_the_line_is_wired(void)
{
    // A wired GDO0 is held LOW by the radio while it is IDLE.
    nativeCC1101Install();
    nativeCC1101().gdo0Connected = true;

    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));

    cc1101_diagnostics_t diag;
    cc1101_collect_diagnostics(&diag);
    TEST_ASSERT_FALSE(diag.gdo0_disconnected);
    TEST_ASSERT_EQUAL_INT(0, diag.gdo0_level);
}

void test_gdo0_self_test_detects_a_pin_pointed_at_nothing(void)
{
    // The reported failure mode: gdo0_pin set to a GPIO the CC1101 is not on.
    // The pull-up holds it HIGH, so every sync-word wait returns instantly and
    // the "frames" that follow are noise. Nothing else in the driver notices.
    nativeCC1101Install();
    nativeCC1101().gdo0Connected = false;

    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency)); // SPI is fine; only GDO0 is wrong

    cc1101_diagnostics_t diag;
    cc1101_collect_diagnostics(&diag);
    TEST_ASSERT_TRUE(diag.gdo0_disconnected);
    TEST_ASSERT_EQUAL_INT(1, diag.gdo0_level);
}

void test_gdo0_verdict_clears_once_the_line_is_fixed(void)
{
    // The verdict is state, not a latch: re-running init on a corrected board
    // must clear it, otherwise the diagnostic report would keep accusing a
    // wiring fault that no longer exists.
    nativeCC1101Install();
    nativeCC1101().gdo0Connected = false;
    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));

    cc1101_diagnostics_t diag;
    cc1101_collect_diagnostics(&diag);
    TEST_ASSERT_TRUE(diag.gdo0_disconnected);

    nativeCC1101().gdo0Connected = true;
    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));

    cc1101_collect_diagnostics(&diag);
    TEST_ASSERT_FALSE(diag.gdo0_disconnected);
}
