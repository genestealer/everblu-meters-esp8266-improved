/**
 * @file test_esphome_meter_reader.cpp
 * @brief Host tests for the ESPHome-only behaviour of MeterReader
 *
 * MeterReader compiles differently for the two deployment targets. The
 * test_native_meter_reader suite covers the MQTT build; the branches guarded by
 * USE_ESPHOME were compiled here but never executed by any test, which is most
 * of what kept meter_reader.cpp off full coverage.
 *
 * Two differences matter to users:
 *
 *  - On boot the ESPHome build deliberately does not publish idle/zero states.
 *    ESPHome restores sensor values from Home Assistant, and publishing a fresh
 *    zero would overwrite the restored reading and put a false drop in the
 *    history graph. A radio failure is still reported immediately, because that
 *    is the one state the user needs to see straight away.
 *  - Scheduled reads are held until the Home Assistant API connects, so the
 *    first reading of the day is not lost to a publish that goes nowhere.
 */

#include <unity.h>

#include <Arduino.h>

#include "native_fakes.h"
#include "services/meter_reader.h"
// frequency_manager.h skips this include in ESPHome builds, but the fake
// storage backend is still what the calibration is read from here.
#include "services/storage_abstraction.h"

namespace
{
    FakeConfig g_config;
    FakeTime g_time;
    RecordingPublisher g_publisher;
}

void esphomeReaderSetUp()
{
    resetAllFakes();
    g_config = FakeConfig();
    g_time = FakeTime();
    g_publisher.reset();
}

// ---------------------------------------------------------------------------
// Boot behaviour
// ---------------------------------------------------------------------------

void test_esphome_begin_does_not_publish_idle_states(void)
{
    MeterReader reader(&g_config, &g_time, &g_publisher);
    reader.begin();

    TEST_ASSERT_TRUE(reader.isRadioConnected());

    // None of these may be published on a healthy boot: ESPHome restores them
    // from Home Assistant and a fresh value would overwrite the restored one.
    TEST_ASSERT_EQUAL(0, (int)g_publisher.radioStates.size());
    TEST_ASSERT_EQUAL(0, (int)g_publisher.statuses.size());
    TEST_ASSERT_EQUAL(0, (int)g_publisher.errors.size());
    TEST_ASSERT_EQUAL(0, (int)g_publisher.activeReadingFlags.size());
    TEST_ASSERT_EQUAL(0, (int)g_publisher.statistics.size());
    TEST_ASSERT_EQUAL(0, (int)g_publisher.readings.size());
}

void test_esphome_begin_publishes_settings_and_calibration(void)
{
    // The stored calibration is published at boot so it is visible in Home
    // Assistant immediately, confirming it survived the reboot.
    StorageAbstraction::saveFloat("freq_offset", 0.0125f, 0xABCD);
    g_config.frequency = 433.82f;

    MeterReader reader(&g_config, &g_time, &g_publisher);
    reader.begin();

    TEST_ASSERT_EQUAL(1, g_publisher.settingsPublishes);
    TEST_ASSERT_EQUAL(1, (int)g_publisher.frequencyOffsets.size());
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.0125f, g_publisher.frequencyOffsets.back());
    TEST_ASSERT_EQUAL(1, (int)g_publisher.tunedFrequencies.size());
    // Loose tolerance: 433.8325 is at the edge of what a 32-bit float resolves.
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 433.8325f, g_publisher.tunedFrequencies.back());
}

void test_esphome_begin_reports_a_radio_failure_immediately(void)
{
    fakeRadio().initSucceeds = false;

    MeterReader reader(&g_config, &g_time, &g_publisher);
    reader.begin();

    TEST_ASSERT_FALSE(reader.isRadioConnected());
    TEST_ASSERT_EQUAL_STRING("unavailable", g_publisher.lastRadioState().c_str());
    TEST_ASSERT_EQUAL_STRING("Error", g_publisher.lastStatus().c_str());
    TEST_ASSERT_EQUAL_STRING("CC1101 radio not responding - check SPI wiring",
                             g_publisher.lastError().c_str());
}

void test_esphome_begin_without_a_publisher_is_safe(void)
{
    MeterReader reader(&g_config, &g_time, nullptr);
    reader.begin();

    TEST_ASSERT_TRUE(reader.isRadioConnected());
}

// ---------------------------------------------------------------------------
// Home Assistant connection gating
// ---------------------------------------------------------------------------

void test_esphome_scheduled_read_waits_for_home_assistant(void)
{
    g_config.schedule = "Monday-Friday";
    g_config.readHourUTC = 10;
    g_config.readMinuteUTC = 0;
    fakeRadio().responses.push_back(FakeRadio::success());

    MeterReader reader(&g_config, &g_time, &g_publisher);
    reader.begin();
    g_publisher.reset();

    // 2025-06-10 is a Tuesday. The schedule matches, but Home Assistant has not
    // connected yet, so the read must not start.
    g_time.setUtc(2025, 6, 10, 10, 0, 0);
    nativeClockAdvance(1000);
    reader.loop();
    TEST_ASSERT_EQUAL(0, (int)fakeRadio().calls.size());

    reader.setHAConnected(true);
    nativeClockAdvance(1000);
    reader.loop();
    TEST_ASSERT_EQUAL(1, (int)fakeRadio().calls.size());
}

void test_esphome_losing_home_assistant_stops_scheduled_reads(void)
{
    g_config.schedule = "Monday-Friday";
    fakeRadio().responses.push_back(FakeRadio::success());

    MeterReader reader(&g_config, &g_time, &g_publisher);
    reader.begin();
    reader.setHAConnected(true);
    g_publisher.reset();

    g_time.setUtc(2025, 6, 10, 10, 0, 0);
    nativeClockAdvance(1000);
    reader.loop();
    TEST_ASSERT_EQUAL(1, (int)fakeRadio().calls.size());

    reader.setHAConnected(false);
    g_time.setUtc(2025, 6, 11, 10, 0, 0);
    nativeClockAdvance(1000);
    reader.loop();
    TEST_ASSERT_EQUAL(1, (int)fakeRadio().calls.size());
}

void test_esphome_manual_read_does_not_wait_for_home_assistant(void)
{
    // The gate applies to the schedule only: a button press must still work, so
    // a user can diagnose the device before Home Assistant is up.
    fakeRadio().responses.push_back(FakeRadio::success(4242));

    MeterReader reader(&g_config, &g_time, &g_publisher);
    reader.begin();
    g_publisher.reset();

    reader.triggerReading(false);

    TEST_ASSERT_EQUAL(1, (int)fakeRadio().calls.size());
    TEST_ASSERT_EQUAL(1, (int)g_publisher.readings.size());
    TEST_ASSERT_EQUAL(4242, g_publisher.readings[0].data.volume);
}

// ---------------------------------------------------------------------------
// Reading behaviour, ESPHome build
// ---------------------------------------------------------------------------

void test_esphome_successful_read_publishes_the_same_sequence(void)
{
    fakeRadio().responses.push_back(FakeRadio::success(56789));

    MeterReader reader(&g_config, &g_time, &g_publisher);
    reader.begin();
    g_publisher.reset();

    reader.triggerReading(false);

    TEST_ASSERT_EQUAL(1, (int)g_publisher.readings.size());
    TEST_ASSERT_EQUAL_STRING("Idle", g_publisher.lastRadioState().c_str());
    TEST_ASSERT_EQUAL_STRING("Reading successful", g_publisher.lastStatus().c_str());
    TEST_ASSERT_FALSE(reader.isReadingInProgress());
    TEST_ASSERT_FALSE(g_publisher.activeReadingFlags.back());
}

void test_esphome_failed_read_reaches_the_cooldown(void)
{
    g_config.maxRetries = 1;
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::CrcFailed));

    MeterReader reader(&g_config, &g_time, &g_publisher);
    reader.begin();
    g_publisher.reset();

    reader.triggerReading(false);

    TEST_ASSERT_EQUAL_STRING("Failed after max retries", g_publisher.lastStatus().c_str());
    TEST_ASSERT_EQUAL_STRING(read_failure_message(ReadFailure::CrcFailed, false),
                             g_publisher.lastError().c_str());

    unsigned long attempts = 0, successes = 0, failures = 0;
    reader.getStatistics(attempts, successes, failures);
    TEST_ASSERT_EQUAL_UINT32(1, attempts);
    TEST_ASSERT_EQUAL_UINT32(1, failures);
}

void test_esphome_stop_reading_returns_to_idle(void)
{
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::NoReply));

    MeterReader reader(&g_config, &g_time, &g_publisher);
    reader.begin();
    g_publisher.reset();

    reader.triggerReading(false);
    TEST_ASSERT_TRUE(reader.isReadingInProgress());

    reader.stopReading();

    TEST_ASSERT_FALSE(reader.isReadingInProgress());
    TEST_ASSERT_EQUAL_STRING("Reading stopped", g_publisher.lastStatus().c_str());
    TEST_ASSERT_EQUAL_STRING("Idle", g_publisher.lastRadioState().c_str());
}

void test_esphome_reset_frequency_offset_retunes_and_publishes(void)
{
    StorageAbstraction::saveFloat("freq_offset", 0.030f, 0xABCD);
    g_config.frequency = 433.82f;

    MeterReader reader(&g_config, &g_time, &g_publisher);
    reader.begin();
    g_publisher.reset();

    reader.resetFrequencyOffset();

    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 433.82f, fakeRadio().lastInitFrequency());
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.0f, g_publisher.frequencyOffsets.back());
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 433.82f, g_publisher.tunedFrequencies.back());
}

void test_esphome_frequency_scan_publishes_the_new_offset(void)
{
    g_config.frequency = 433.82f;

    MeterReader reader(&g_config, &g_time, &g_publisher);
    reader.begin();
    g_publisher.reset();

    // A carrier 20 kHz above the base frequency, within the narrow scan range.
    fakeRadio().carrierFrequency = 433.84f;
    fakeRadio().carrierWidthMHz = 0.006f;

    reader.performFrequencyScan();

    TEST_ASSERT_FALSE(g_publisher.frequencyOffsets.empty());
    TEST_ASSERT_TRUE(FrequencyManager::getOffset() > 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, FrequencyManager::getOffset(),
                             g_publisher.frequencyOffsets.back());
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, FrequencyManager::getTunedFrequency(),
                             g_publisher.tunedFrequencies.back());
}

void test_esphome_statistics_are_republished_periodically(void)
{
    MeterReader reader(&g_config, &g_time, &g_publisher);
    reader.begin();
    reader.setHAConnected(true);
    g_publisher.reset();

    reader.loop();
    const int baseline = (int)g_publisher.statistics.size();

    nativeClockAdvance(300000);
    reader.loop();

    TEST_ASSERT_EQUAL(baseline + 1, (int)g_publisher.statistics.size());
}
