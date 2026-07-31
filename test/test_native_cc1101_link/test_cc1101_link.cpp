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

#include <cstring>

#include "native_cc1101_device.h"
#include "core/cc1101.h"

static constexpr float kTestFrequency = 433.82f;

/** SYNC1/SYNC0, the scratch pair the probe writes through. */
static constexpr uint8_t kSync1Register = 0x04;
static constexpr uint8_t kSync0Register = 0x05;

/** Assert that the diagnostic report contains @p needle, quoting it on failure. */
static void assertReportContains(const char *report, const char *needle)
{
    char message[192];
    snprintf(message, sizeof(message), "report does not contain: %s", needle);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(report, needle), message);
}

/** A fully populated context, as the MQTT build passes it. */
static cc1101_report_context_t makeReportContext()
{
    cc1101_report_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.meter_code = "21-0123456";
    ctx.meter_year = 21;
    ctx.meter_serial = 123456;
    ctx.is_gas = false;
    ctx.configured_frequency_mhz = kTestFrequency;
    ctx.rx_attenuation_db = 6;
    ctx.meter_initialised = true;
    return ctx;
}

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

void test_marcstate_names_cover_every_datasheet_state(void)
{
    // Table 25 defines every value 0x00-0x16; a wrong entry here would misreport a real
    // MARCSTATE as something else in the diagnostic report, so every case is worth pinning
    // down rather than trusting the handful of values a report happens to show.
    static const struct
    {
        uint8_t value;
        const char *name;
    } kStates[] = {
        {0x00, "SLEEP"},        {0x01, "IDLE"},          {0x02, "XOFF"},
        {0x03, "VCOON_MC"},     {0x04, "REGON_MC"},      {0x05, "MANCAL"},
        {0x06, "VCOON"},        {0x07, "REGON"},         {0x08, "STARTCAL"},
        {0x09, "BWBOOST"},      {0x0A, "FS_LOCK"},       {0x0B, "IFADCON"},
        {0x0C, "ENDCAL"},       {0x0D, "RX"},            {0x0E, "RX_END"},
        {0x0F, "RX_RST"},       {0x10, "TXRX_SWITCH"},   {0x11, "RXFIFO_OVERFLOW"},
        {0x12, "FSTXON"},       {0x13, "TX"},            {0x14, "TX_END"},
        {0x15, "RXTX_SWITCH"},  {0x16, "TXFIFO_UNDERFLOW"},
    };

    for (const auto &state : kStates)
    {
        char message[32];
        snprintf(message, sizeof(message), "MARCSTATE 0x%02X", state.value);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(state.name, cc1101_marcstate_name(state.value), message);
    }
}

void test_marcstate_name_masks_off_the_unused_upper_bits(void)
{
    // MARCSTATE is a 5-bit field (bits 5-7 are reserved/read as other status). A caller
    // passing the raw register byte must still get the right name even if a reserved bit
    // happens to be set.
    TEST_ASSERT_EQUAL_STRING("IDLE", cc1101_marcstate_name(0xE1));
    TEST_ASSERT_EQUAL_STRING("TXFIFO_UNDERFLOW", cc1101_marcstate_name(0x36));
}

void test_freq_registers_decode_to_the_tuned_carrier(void)
{
    // Taken from a real report: FREQ2/1/0 = 0x10 0xAF 0x65 on a device whose
    // configured base was 433.782715 MHz. The ~31 kHz difference is the stored
    // calibration offset, and showing only the base hid it completely.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 433.8134f, cc1101_freq_registers_to_mhz(0x10, 0xAF, 0x65));
}

void test_diagnostics_report_where_the_radio_is_actually_tuned(void)
{
    // The decoded carrier has to come from the registers the radio is running
    // on, not from the value the caller asked for, or it could never disagree.
    nativeCC1101Install();
    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));

    cc1101_diagnostics_t diag;
    cc1101_collect_diagnostics(&diag);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, kTestFrequency, diag.carrier_mhz);
    TEST_ASSERT_EQUAL_FLOAT(cc1101_freq_registers_to_mhz(diag.freq2, diag.freq1, diag.freq0), diag.carrier_mhz);
}

// ---------------------------------------------------------------------------
// GDO0 wiring self-test
// ---------------------------------------------------------------------------

void test_gdo0_self_test_is_reported_as_not_run_before_the_first_init(void)
{
    // MUST RUN FIRST (see test_runner.cpp): the verdict is process-wide state, and this
    // is the only point at which no init has happened yet.
    //
    // The report button deliberately works without the meter reader, so it is usually
    // pressed precisely when the radio never came up - i.e. when cc1101_init() has not
    // run. Defaulting the verdict to "passed" there would clear GDO0 of a fault that was
    // never actually checked, which is worse than saying nothing.
    nativeCC1101Install();

    cc1101_diagnostics_t diag;
    cc1101_collect_diagnostics(&diag);

    TEST_ASSERT_EQUAL_INT(CC1101_SELFTEST_NOT_RUN, diag.gdo0_selftest);
    // The pins have not been given a mode either, so their levels are unknown rather than
    // whatever a floating input happened to settle at.
    TEST_ASSERT_EQUAL_INT(-1, diag.gdo0_level);
    TEST_ASSERT_EQUAL_INT(-1, diag.gdo2_level);

    // Same reasoning for the printed report: it must say the check has not run, and it
    // must not present an unconfigured pin's level as a reading.
    const char *report = cc1101_print_diagnostic_report(nullptr);
    assertReportContains(report, "GDO0 wiring self-test: NOT RUN");
    assertReportContains(report, "unknown (pin not configured yet)");
}

void test_gdo0_self_test_passes_when_the_line_is_wired(void)
{
    // A wired GDO0 is held LOW by the radio while it is IDLE.
    nativeCC1101Install();
    nativeCC1101().gdo0Connected = true;

    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));

    cc1101_diagnostics_t diag;
    cc1101_collect_diagnostics(&diag);
    TEST_ASSERT_EQUAL_INT(CC1101_SELFTEST_PASSED, diag.gdo0_selftest);
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
    TEST_ASSERT_EQUAL_INT(CC1101_SELFTEST_FAILED, diag.gdo0_selftest);
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
    TEST_ASSERT_EQUAL_INT(CC1101_SELFTEST_FAILED, diag.gdo0_selftest);

    nativeCC1101().gdo0Connected = true;
    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));

    cc1101_collect_diagnostics(&diag);
    TEST_ASSERT_EQUAL_INT(CC1101_SELFTEST_PASSED, diag.gdo0_selftest);
}

void test_diagnostics_park_the_radio_and_put_it_back_in_rx(void)
{
    // cc1101_init() ends in cc1101_rec_mode(), so the radio is normally sitting in RX when
    // the report button is pressed. The link probe has to write SYNC1/SYNC0, which the
    // datasheet only allows from IDLE, so the radio is parked for the probe - but leaving
    // it in IDLE would silently deafen the parked receiver, which is the opposite of what
    // a diagnostic should do.
    nativeCC1101Install();
    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));
    TEST_ASSERT_EQUAL_HEX8(0x0D, nativeCC1101().marcstate); // RX: where init leaves it

    cc1101_diagnostics_t diag;
    cc1101_collect_diagnostics(&diag);

    // The snapshot reports where the radio was, not where the probe had to move it.
    TEST_ASSERT_EQUAL_HEX8(0x0D, diag.marcstate);
    TEST_ASSERT_EQUAL_HEX8(0x0D, nativeCC1101().marcstate);
}

void test_diagnostics_leave_an_idle_radio_idle(void)
{
    // The converse of the above: a radio that was not receiving must not be strobed into
    // RX as a side effect of asking it for a report.
    nativeCC1101Install();
    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));
    nativeCC1101().marcstate = 0x01; // IDLE, as if a read had just finished

    cc1101_diagnostics_t diag;
    cc1101_collect_diagnostics(&diag);

    TEST_ASSERT_EQUAL_HEX8(0x01, diag.marcstate);
    TEST_ASSERT_EQUAL_HEX8(0x01, nativeCC1101().marcstate);
}

// ---------------------------------------------------------------------------
// cc1101_print_diagnostic_report: the text a user pastes into a bug report
// ---------------------------------------------------------------------------

void test_report_states_the_configuration_it_was_given(void)
{
    // The report is the whole point of the diagnostics: whatever the caller knows about
    // the configuration has to reach the text, or a bug report arrives half-empty.
    nativeCC1101Install();
    nativeCC1101().gdo0Connected = true;
    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));

    cc1101_report_context_t ctx = makeReportContext();
    ctx.cs_pin_text = "managed by the ESPHome SPI bus";
    ctx.gdo0_pin_text = "GPIO27 (inverted)";
    ctx.gdo2_pin_text = "GPIO26";

    const char *report = cc1101_print_diagnostic_report(&ctx);
    TEST_ASSERT_NOT_NULL(report);

    assertReportContains(report, "===== EverBlu diagnostic report =====");
    assertReportContains(report, "Meter Code: 21-0123456 (year=21, serial=123456, Water)");
    assertReportContains(report, "Configured Frequency: 433.82");
    assertReportContains(report, "RX Attenuation: 6 dB");
    // Caller-supplied pin text wins over the driver's own numbers: ESPHome knows about
    // inversion and expanders, the driver only ever sees a GPIO number.
    assertReportContains(report, "CS Pin: managed by the ESPHome SPI bus, GDO0 Pin: GPIO27 (inverted), GDO2 Pin: GPIO26");
    assertReportContains(report, "SPI Link Self-Test: PASSED");
    assertReportContains(report, "PARTNUM: 0x00 (expect 0x00), VERSION: 0x14 (expect 0x04 or 0x14)");
    assertReportContains(report, "MARCSTATE: 0x0D (RX)");
    assertReportContains(report, "GDO0 wiring self-test: passed");
    assertReportContains(report, "Meter reader initialised: yes");
    assertReportContains(report, "===== end of report =====");
}

void test_report_distinguishes_a_gas_meter_and_an_uninitialised_reader(void)
{
    // Both flags read as their opposite when left at the struct's zero value, so a
    // report that only ever showed "Water"/"yes" would look correct while being wrong.
    nativeCC1101Install();
    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));

    cc1101_report_context_t ctx = makeReportContext();
    ctx.is_gas = true;
    ctx.meter_initialised = false;

    const char *report = cc1101_print_diagnostic_report(&ctx);
    assertReportContains(report, "serial=123456, Gas)");
    assertReportContains(report, "Meter reader initialised: no");
}

void test_report_falls_back_to_the_pins_the_driver_is_using(void)
{
    // The standalone build has no pin objects to describe, so it passes nothing and the
    // driver has to name the pins it is actually driving - a report that said nothing
    // about the pins would be useless for the wiring faults it exists to diagnose.
    nativeCC1101Install();
    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));

    cc1101_report_context_t ctx = makeReportContext();
    ctx.meter_code = nullptr;

    const char *report = cc1101_print_diagnostic_report(&ctx);
    assertReportContains(report, "Meter Code: unknown ");
    assertReportContains(report, "CS Pin: GPIO15 (hardware SPI SS), GDO0 Pin: GPIO5, GDO2 Pin: GPIO4");
}

void test_report_works_without_any_context(void)
{
    // The button deliberately works when setup never finished, which is exactly when
    // there is no configuration to hand. A missing context must not cost the radio half
    // of the report, and must certainly not crash.
    nativeCC1101Install();
    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));

    const char *report = cc1101_print_diagnostic_report(nullptr);
    TEST_ASSERT_NOT_NULL(report);
    assertReportContains(report, "Meter Code: unknown (year=0, serial=0, Water)");
    assertReportContains(report, "SPI Link Self-Test: PASSED");
    assertReportContains(report, "===== end of report =====");
}

void test_report_says_the_register_values_are_meaningless_on_a_stuck_bus(void)
{
    // The fault this whole suite exists for: every register reads back the same byte. The
    // report must lead with that, or the values below it send people chasing a radio
    // problem that is really a wiring problem.
    nativeCC1101Install();
    nativeCC1101().fault = NativeSpiFault::StuckConstant;
    nativeCC1101().stuckValue = 0x0F;

    cc1101_report_context_t ctx = makeReportContext();
    const char *report = cc1101_print_diagnostic_report(&ctx);

    assertReportContains(report, "SPI Link Self-Test: FAILED - the register values below are meaningless");
    assertReportContains(report, "PARTNUM: 0x0F (expect 0x00), VERSION: 0x0F (expect 0x04 or 0x14)");
}

void test_report_calls_out_a_gdo0_line_that_is_not_wired(void)
{
    // A failed GDO0 self-test is the single most useful line in the report, because
    // nothing else in the firmware notices the fault.
    nativeCC1101Install();
    nativeCC1101().gdo0Connected = false;
    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));

    cc1101_report_context_t ctx = makeReportContext();
    const char *report = cc1101_print_diagnostic_report(&ctx);

    assertReportContains(report, "GDO0 wiring self-test: FAILED - GDO0 looks unconnected or on the wrong GPIO");
    assertReportContains(report, "GDO0 level: HIGH (expect LOW while idle)");
}

void test_report_is_truncated_rather_than_overrunning_its_buffer(void)
{
    // The meter code is caller-supplied text of unbounded length. snprintf reports what it
    // would have written, so an unclamped offset would walk past the end of the buffer and
    // corrupt whatever follows it.
    nativeCC1101Install();
    TEST_ASSERT_TRUE(cc1101_init(kTestFrequency));

    char long_code[CC1101_REPORT_BUFFER_SIZE * 2];
    memset(long_code, 'A', sizeof(long_code) - 1);
    long_code[sizeof(long_code) - 1] = '\0';

    cc1101_report_context_t ctx = makeReportContext();
    ctx.meter_code = long_code;

    const char *report = cc1101_print_diagnostic_report(&ctx);
    TEST_ASSERT_NOT_NULL(report);
    TEST_ASSERT_LESS_THAN_UINT(CC1101_REPORT_BUFFER_SIZE, (unsigned) strlen(report));
    assertReportContains(report, "===== EverBlu diagnostic report =====");
}
