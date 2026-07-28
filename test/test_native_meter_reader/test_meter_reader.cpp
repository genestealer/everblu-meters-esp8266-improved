/**
 * @file test_meter_reader.cpp
 * @brief Host tests for the MeterReader orchestrator
 *
 * MeterReader owns the retry sequence, the post-failure cooldown, the schedule
 * gate and the order in which results reach Home Assistant. All of that is
 * reachable on the host through the injected interfaces plus the fake CC1101
 * driver in fakes.cpp, so none of it needs a radio to test.
 */

#include <unity.h>

#include <Arduino.h>

#include "native_fakes.h"
#include "services/meter_reader.h"

namespace
{
    FakeConfig g_config;
    FakeTime g_time;
    RecordingPublisher g_publisher;

    // Retry delay constant mirrored from meter_reader.cpp (file-local there).
    constexpr unsigned long RETRY_DELAY_MS = 5000;

    /// Construct a reader wired to the shared fakes and run begin().
    MeterReader makeReader()
    {
        MeterReader reader(&g_config, &g_time, &g_publisher);
        reader.begin();
        g_publisher.reset(); // Discard the boot-time publishes
        return reader;
    }

    /// Run loop() enough times to cross a retry deadline that has expired.
    void advanceAndLoop(MeterReader &reader, unsigned long ms)
    {
        nativeClockAdvance(ms);
        reader.loop();
    }

    /// Step the auto-started frequency scan to completion, as the host loop does.
    void drainFrequencyScan(MeterReader &reader)
    {
        int guard = 0;
        while (FrequencyManager::isScanInProgress() && guard++ < 5000)
        {
            reader.loop();
        }
        reader.loop(); // One more pass so the reader publishes the resulting tuning
    }
}

void meterReaderSetUp()
{
    resetAllFakes();
    g_config = FakeConfig();
    g_time = FakeTime();
    g_publisher.reset();
}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

void test_begin_reports_radio_failure(void)
{
    fakeRadio().initSucceeds = false;

    MeterReader reader(&g_config, &g_time, &g_publisher);
    reader.begin();

    TEST_ASSERT_FALSE(reader.isRadioConnected());
    TEST_ASSERT_EQUAL_STRING("unavailable", g_publisher.lastRadioState().c_str());
    TEST_ASSERT_TRUE(g_publisher.sawStatus("Error"));
    TEST_ASSERT_TRUE(g_publisher.sawError("CC1101 radio not responding"));
}

void test_begin_tunes_radio_to_base_plus_stored_offset(void)
{
    // A calibration of +12 kHz survives from a previous run.
    StorageAbstraction::saveFloat("freq_offset", 0.012f, 0xABCD);
    g_config.frequency = 433.82f;

    MeterReader reader(&g_config, &g_time, &g_publisher);
    reader.begin();

    TEST_ASSERT_TRUE(reader.isRadioConnected());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 433.832f, fakeRadio().lastInitFrequency());
}

void test_begin_converts_utc_reading_time_to_local(void)
{
    // 23:30 UTC with a +90 minute offset lands at 01:00 the next local day.
    g_config.readHourUTC = 23;
    g_config.readMinuteUTC = 30;
    g_config.timezoneOffsetMinutes = 90;
    fakeRadio().responses.push_back(FakeRadio::success());

    MeterReader reader = makeReader();

    g_time.setUtc(2025, 6, 10, 23, 30, 0);
    nativeClockAdvance(1000);
    reader.loop();

    TEST_ASSERT_EQUAL(1, (int)fakeRadio().calls.size());
}

// ---------------------------------------------------------------------------
// Successful read
// ---------------------------------------------------------------------------

void test_successful_read_publishes_once_and_returns_to_idle(void)
{
    fakeRadio().responses.push_back(FakeRadio::success(98765));

    MeterReader reader = makeReader();
    reader.triggerReading(false);

    TEST_ASSERT_EQUAL(1, (int)g_publisher.readings.size());
    TEST_ASSERT_EQUAL(98765, g_publisher.readings[0].data.volume);
    TEST_ASSERT_FALSE(reader.isReadingInProgress());
    TEST_ASSERT_EQUAL_STRING("Idle", g_publisher.lastRadioState().c_str());
    TEST_ASSERT_EQUAL_STRING("Reading successful", g_publisher.lastStatus().c_str());

    // Active reading must be raised then cleared exactly once.
    TEST_ASSERT_EQUAL(2, (int)g_publisher.activeReadingFlags.size());
    TEST_ASSERT_TRUE(g_publisher.activeReadingFlags[0]);
    TEST_ASSERT_FALSE(g_publisher.activeReadingFlags[1]);

    unsigned long attempts = 0, successes = 0, failures = 0;
    reader.getStatistics(attempts, successes, failures);
    TEST_ASSERT_EQUAL_UINT32(1, attempts);
    TEST_ASSERT_EQUAL_UINT32(1, successes);
    TEST_ASSERT_EQUAL_UINT32(0, failures);
}

void test_successful_read_passes_configured_meter_identity(void)
{
    g_config.meterYear = 19;
    g_config.meterSerial = 987654;
    fakeRadio().responses.push_back(FakeRadio::success());

    MeterReader reader = makeReader();
    reader.triggerReading(false);

    TEST_ASSERT_EQUAL(1, (int)fakeRadio().calls.size());
    TEST_ASSERT_EQUAL_UINT8(19, fakeRadio().calls[0].year);
    TEST_ASSERT_EQUAL_UINT32(987654, fakeRadio().calls[0].serial);
}

void test_successful_read_publishes_history_only_when_available(void)
{
    tmeter_data withHistory = FakeRadio::success();
    withHistory.history_available = true;
    for (int i = 0; i < 13; i++)
    {
        withHistory.history[i] = (uint32_t)(1000 + i * 10);
    }
    fakeRadio().responses.push_back(withHistory);
    fakeRadio().responses.push_back(FakeRadio::success()); // history_available = false

    MeterReader reader = makeReader();
    reader.triggerReading(false);
    TEST_ASSERT_EQUAL(1, g_publisher.historyPublishes);

    reader.triggerReading(false);
    TEST_ASSERT_EQUAL(1, g_publisher.historyPublishes);
}

void test_reading_is_skipped_when_publisher_not_ready(void)
{
    fakeRadio().responses.push_back(FakeRadio::success());
    MeterReader reader = makeReader();

    g_publisher.ready = false;
    reader.triggerReading(false);

    TEST_ASSERT_EQUAL(0, (int)fakeRadio().calls.size());
    TEST_ASSERT_EQUAL(0, (int)g_publisher.readings.size());
    TEST_ASSERT_FALSE(reader.isReadingInProgress());
}

// ---------------------------------------------------------------------------
// Retry sequence
// ---------------------------------------------------------------------------

void test_failed_read_schedules_retry_after_delay(void)
{
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::NoReply));

    MeterReader reader = makeReader();
    reader.triggerReading(false);

    TEST_ASSERT_EQUAL(1, (int)fakeRadio().calls.size());
    TEST_ASSERT_TRUE(reader.isReadingInProgress());
    TEST_ASSERT_EQUAL_STRING("Retry scheduled", g_publisher.lastStatus().c_str());

    // Nothing happens before the delay expires.
    advanceAndLoop(reader, RETRY_DELAY_MS - 1);
    TEST_ASSERT_EQUAL(1, (int)fakeRadio().calls.size());

    advanceAndLoop(reader, 1);
    TEST_ASSERT_EQUAL(2, (int)fakeRadio().calls.size());
}

void test_retry_sequence_keeps_active_reading_raised_until_it_ends(void)
{
    g_config.maxRetries = 3;
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::NoReply));
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::NoReply));
    fakeRadio().responses.push_back(FakeRadio::success());

    MeterReader reader = makeReader();
    reader.triggerReading(false);
    advanceAndLoop(reader, RETRY_DELAY_MS);
    advanceAndLoop(reader, RETRY_DELAY_MS);

    TEST_ASSERT_EQUAL(3, (int)fakeRadio().calls.size());
    TEST_ASSERT_EQUAL(1, (int)g_publisher.readings.size());

    // The sensor is cleared exactly once, at the very end: it must not drop to
    // false between attempts and make the sequence look finished.
    int clears = 0;
    for (bool flag : g_publisher.activeReadingFlags)
    {
        if (!flag)
        {
            clears++;
        }
    }
    TEST_ASSERT_EQUAL(1, clears);
    TEST_ASSERT_FALSE(g_publisher.activeReadingFlags.back());
    TEST_ASSERT_EQUAL_STRING("Idle", g_publisher.lastRadioState().c_str());
}

void test_read_gives_up_after_max_retries(void)
{
    g_config.maxRetries = 3;
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::NoReply));

    MeterReader reader = makeReader();
    reader.triggerReading(false);
    advanceAndLoop(reader, RETRY_DELAY_MS);
    advanceAndLoop(reader, RETRY_DELAY_MS);

    TEST_ASSERT_EQUAL(3, (int)fakeRadio().calls.size());
    TEST_ASSERT_FALSE(reader.isReadingInProgress());
    TEST_ASSERT_EQUAL_STRING("Failed after max retries", g_publisher.lastStatus().c_str());
    TEST_ASSERT_EQUAL_STRING("Idle", g_publisher.lastRadioState().c_str());

    // No further attempts once the sequence has ended.
    advanceAndLoop(reader, RETRY_DELAY_MS * 4);
    TEST_ASSERT_EQUAL(3, (int)fakeRadio().calls.size());

    unsigned long attempts = 0, successes = 0, failures = 0;
    reader.getStatistics(attempts, successes, failures);
    TEST_ASSERT_EQUAL_UINT32(3, attempts);
    TEST_ASSERT_EQUAL_UINT32(0, successes);
    TEST_ASSERT_EQUAL_UINT32(1, failures);
}

void test_single_retry_configuration_fails_immediately(void)
{
    g_config.maxRetries = 1;
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::NoReply));

    MeterReader reader = makeReader();
    reader.triggerReading(false);

    TEST_ASSERT_EQUAL(1, (int)fakeRadio().calls.size());
    TEST_ASSERT_FALSE(reader.isReadingInProgress());
    TEST_ASSERT_EQUAL_STRING("Failed after max retries", g_publisher.lastStatus().c_str());
}

void test_final_error_keeps_the_most_informative_failure(void)
{
    // A run of corrupted frames ending in one silent timeout is still an RF
    // quality problem, so the final message must not fall back to "no response".
    g_config.maxRetries = 3;
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::CrcFailed));
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::CrcFailed));
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::NoReply));

    MeterReader reader = makeReader();
    reader.triggerReading(false);
    advanceAndLoop(reader, RETRY_DELAY_MS);
    advanceAndLoop(reader, RETRY_DELAY_MS);

    TEST_ASSERT_EQUAL_STRING(read_failure_message(ReadFailure::CrcFailed, false),
                             g_publisher.lastError().c_str());
}

void test_no_reply_failure_reports_the_no_response_message(void)
{
    g_config.maxRetries = 2;
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::NoReply));

    MeterReader reader = makeReader();
    reader.triggerReading(false);
    advanceAndLoop(reader, RETRY_DELAY_MS);

    TEST_ASSERT_EQUAL_STRING(read_failure_message(ReadFailure::NoReply, false),
                             g_publisher.lastError().c_str());
}

void test_success_after_failures_clears_the_error(void)
{
    g_config.maxRetries = 3;
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::CrcFailed));
    fakeRadio().responses.push_back(FakeRadio::success());

    MeterReader reader = makeReader();
    reader.triggerReading(false);
    advanceAndLoop(reader, RETRY_DELAY_MS);

    TEST_ASSERT_EQUAL_STRING("None", reader.getLastError());

    // The retry counter must be clear, so the next failure starts from attempt 1.
    fakeRadio().responses.clear();
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::NoReply));
    fakeRadio().calls.clear();
    reader.triggerReading(false);
    TEST_ASSERT_TRUE(reader.isReadingInProgress());
    TEST_ASSERT_EQUAL_STRING("Retry scheduled", g_publisher.lastStatus().c_str());
}

void test_zero_volume_reading_counts_as_a_failure(void)
{
    // A frame that decodes to zero volume carries no usable index, so it must
    // be treated as a failed attempt rather than published as a real reading.
    tmeter_data zeroVolume = FakeRadio::success();
    zeroVolume.volume = 0;
    g_config.maxRetries = 1;
    fakeRadio().responses.push_back(zeroVolume);

    MeterReader reader = makeReader();
    reader.triggerReading(false);

    TEST_ASSERT_EQUAL(0, (int)g_publisher.readings.size());
    TEST_ASSERT_EQUAL_STRING("Failed after max retries", g_publisher.lastStatus().c_str());
}

// ---------------------------------------------------------------------------
// Concurrency guard and manual stop
// ---------------------------------------------------------------------------

void test_trigger_is_ignored_while_a_sequence_is_running(void)
{
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::NoReply));

    MeterReader reader = makeReader();
    reader.triggerReading(false);
    TEST_ASSERT_TRUE(reader.isReadingInProgress());

    reader.triggerReading(false);
    TEST_ASSERT_EQUAL(1, (int)fakeRadio().calls.size());
}

void test_stop_cancels_a_pending_retry(void)
{
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::NoReply));

    MeterReader reader = makeReader();
    reader.triggerReading(false);
    reader.stopReading();

    TEST_ASSERT_FALSE(reader.isReadingInProgress());
    TEST_ASSERT_EQUAL_STRING("Reading stopped", g_publisher.lastStatus().c_str());
    TEST_ASSERT_EQUAL_STRING("Idle", g_publisher.lastRadioState().c_str());

    advanceAndLoop(reader, RETRY_DELAY_MS * 4);
    TEST_ASSERT_EQUAL(1, (int)fakeRadio().calls.size());
}

void test_stop_when_idle_does_not_publish_state(void)
{
    MeterReader reader = makeReader();
    reader.stopReading();

    TEST_ASSERT_EQUAL(0, (int)g_publisher.statuses.size());
    TEST_ASSERT_EQUAL(0, (int)g_publisher.radioStates.size());
}

// ---------------------------------------------------------------------------
// Scheduling
// ---------------------------------------------------------------------------

void test_scheduled_read_triggers_once_at_the_configured_time(void)
{
    // 2025-06-10 is a Tuesday.
    g_config.schedule = "Monday-Friday";
    g_config.readHourUTC = 10;
    g_config.readMinuteUTC = 0;
    fakeRadio().responses.push_back(FakeRadio::success());

    MeterReader reader = makeReader();

    g_time.setUtc(2025, 6, 10, 9, 59, 59);
    nativeClockAdvance(1000);
    reader.loop();
    TEST_ASSERT_EQUAL(0, (int)fakeRadio().calls.size());

    g_time.setUtc(2025, 6, 10, 10, 0, 0);
    nativeClockAdvance(1000);
    reader.loop();
    TEST_ASSERT_EQUAL(1, (int)fakeRadio().calls.size());

    // Edge detection: staying inside the same minute must not re-trigger.
    nativeClockAdvance(1000);
    reader.loop();
    TEST_ASSERT_EQUAL(1, (int)fakeRadio().calls.size());
}

void test_scheduled_read_is_skipped_on_a_non_reading_day(void)
{
    // 2025-06-08 is a Sunday.
    g_config.schedule = "Monday-Friday";
    fakeRadio().responses.push_back(FakeRadio::success());

    MeterReader reader = makeReader();

    g_time.setUtc(2025, 6, 8, 10, 0, 0);
    nativeClockAdvance(1000);
    reader.loop();

    TEST_ASSERT_EQUAL(0, (int)fakeRadio().calls.size());
}

void test_reading_day_gate_covers_every_schedule_string(void)
{
    // MeterReader::isReadingDayForConfiguredSchedule() reads m_config live rather
    // than sharing ScheduleManager's static state (each instance can run its own
    // schedule, which multi-meter setups rely on), so it is a second, independent
    // implementation of the day-matching rules and needs its own coverage rather
    // than relying on ScheduleManager's tests.
    //
    // 2025-06-08..14 is Sunday..Saturday, one of each day of the week.
    struct Case
    {
        const char *schedule;
        int day; // 8 = Sunday .. 14 = Saturday
        bool expectRead;
    };
    const Case cases[] = {
        {"Monday-Friday", 8, false},   // Sunday
        {"Monday-Friday", 14, false},  // Saturday
        {"Monday-Friday", 10, true},   // Tuesday
        {"Monday-Saturday", 8, false}, // Sunday
        {"Monday-Saturday", 14, true}, // Saturday
        {"Monday-Sunday", 8, true},    // Sunday
        {"Monday-Sunday", 14, true},   // Saturday
        {"Sunday", 8, true},
        {"Sunday", 9, false},
        {"Monday", 9, true},
        {"Monday", 10, false},
        {"Tuesday", 10, true},
        {"Wednesday", 11, true},
        {"Thursday", 12, true},
        {"Friday", 13, true},
        {"Saturday", 14, true},
        {"Saturday", 8, false},
        {"Whenever I feel like it", 10, false}, // Unknown schedule: always skipped
    };

    for (const Case &c : cases)
    {
        meterReaderSetUp();
        g_config.schedule = c.schedule;
        g_config.readHourUTC = 10;
        g_config.readMinuteUTC = 0;
        fakeRadio().responses.push_back(FakeRadio::success());

        MeterReader reader = makeReader();
        g_time.setUtc(2025, 6, c.day, 10, 0, 0);
        nativeClockAdvance(1000);
        reader.loop();

        char message[96];
        snprintf(message, sizeof(message), "schedule='%s' day=%d expected %s",
                 c.schedule, c.day, c.expectRead ? "a read" : "no read");
        TEST_ASSERT_EQUAL_MESSAGE(c.expectRead ? 1 : 0, (int)fakeRadio().calls.size(), message);
    }
}

void test_scheduled_read_waits_for_time_sync(void)
{
    fakeRadio().responses.push_back(FakeRadio::success());
    MeterReader reader = makeReader();

    g_time.synced = false;
    g_time.setUtc(2025, 6, 10, 10, 0, 0);
    nativeClockAdvance(1000);
    reader.loop();
    TEST_ASSERT_EQUAL(0, (int)fakeRadio().calls.size());

    g_time.synced = true;
    nativeClockAdvance(1000);
    reader.loop();
    TEST_ASSERT_EQUAL(1, (int)fakeRadio().calls.size());
}

void test_cooldown_blocks_scheduled_reads_until_it_expires(void)
{
    g_config.maxRetries = 1;
    g_config.retryCooldownMs = 60000;
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::NoReply));

    MeterReader reader = makeReader();
    reader.triggerReading(false);
    TEST_ASSERT_EQUAL(1, (int)fakeRadio().calls.size());

    // Still inside the cooldown: the schedule must not start another read.
    g_time.setUtc(2025, 6, 10, 10, 0, 0);
    nativeClockAdvance(g_config.retryCooldownMs - 1000);
    reader.loop();
    TEST_ASSERT_EQUAL(1, (int)fakeRadio().calls.size());

    // Cooldown expired: the next matching schedule tick reads again.
    nativeClockAdvance(2000);
    reader.loop();
    TEST_ASSERT_EQUAL(2, (int)fakeRadio().calls.size());
}

void test_cooldown_applies_to_a_failure_at_time_zero(void)
{
    // millis() legitimately returns 0 for the first millisecond after boot, so
    // the cooldown must not be keyed on the timestamp being non-zero.
    nativeClockSet(0);
    g_config.maxRetries = 1;
    g_config.retryCooldownMs = 60000;
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::NoReply));

    MeterReader reader = makeReader();
    reader.triggerReading(false);
    TEST_ASSERT_EQUAL(1, (int)fakeRadio().calls.size());

    g_time.setUtc(2025, 6, 10, 10, 0, 0);
    nativeClockAdvance(1000);
    reader.loop();

    TEST_ASSERT_EQUAL(1, (int)fakeRadio().calls.size());
}

void test_statistics_are_republished_periodically(void)
{
    MeterReader reader = makeReader();

    reader.loop();
    const int baseline = (int)g_publisher.statistics.size();

    nativeClockAdvance(300000);
    reader.loop();

    TEST_ASSERT_EQUAL(baseline + 1, (int)g_publisher.statistics.size());
}

// ---------------------------------------------------------------------------
// Frequency handling driven from the reader
// ---------------------------------------------------------------------------

void test_auto_scan_on_failure_runs_once_per_failure_streak(void)
{
    g_config.maxRetries = 1;
    g_config.autoScanOnFailure = true;
    g_config.retryCooldownMs = 1;
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::NoReply));

    MeterReader reader = makeReader();

    reader.triggerReading(false);
    TEST_ASSERT_TRUE(g_publisher.sawStatus("Auto frequency scan after failed reads"));

    // The scan is stepped from loop(), so it has to finish before the next read.
    drainFrequencyScan(reader);

    g_publisher.reset();
    reader.triggerReading(false);
    TEST_ASSERT_FALSE(g_publisher.sawStatus("Auto frequency scan after failed reads"));
}

void test_auto_scan_on_failure_is_rearmed_by_a_success(void)
{
    g_config.maxRetries = 1;
    g_config.autoScanOnFailure = true;
    g_config.retryCooldownMs = 1;
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::NoReply));

    MeterReader reader = makeReader();
    reader.triggerReading(false);
    drainFrequencyScan(reader);

    fakeRadio().responses.clear();
    fakeRadio().responses.push_back(FakeRadio::success());
    reader.triggerReading(false);

    fakeRadio().responses.clear();
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::NoReply));
    g_publisher.reset();
    reader.triggerReading(false);

    TEST_ASSERT_TRUE(g_publisher.sawStatus("Auto frequency scan after failed reads"));
    drainFrequencyScan(reader);
}

void test_auto_scan_on_failure_stays_off_when_disabled(void)
{
    g_config.maxRetries = 1;
    g_config.autoScanOnFailure = false;
    fakeRadio().responses.push_back(FakeRadio::failure(ReadFailure::NoReply));

    MeterReader reader = makeReader();
    reader.triggerReading(false);

    TEST_ASSERT_FALSE(g_publisher.sawStatus("Auto frequency scan after failed reads"));
}

void test_a_scan_is_only_stepped_by_the_reader_that_started_it(void)
{
    // FrequencyManager holds the calibration in static state shared by every
    // reader on the radio. A second reader must stand down rather than step
    // someone else's scan with its own radio context and meter identity.
    g_config.frequency = 433.82f;

    MeterReader owner = makeReader();
    MeterReader other = makeReader();

    owner.performFrequencyScan();
    TEST_ASSERT_TRUE(FrequencyManager::isScanInProgress());

    const int before = (int)fakeRadio().calls.size();
    other.loop();
    TEST_ASSERT_EQUAL(before, (int)fakeRadio().calls.size());

    owner.loop();
    TEST_ASSERT_EQUAL(before + 1, (int)fakeRadio().calls.size());

    drainFrequencyScan(owner);
}

void test_a_running_scan_blocks_reads_and_further_scan_requests(void)
{
    // The scan owns the radio until it finishes. Anything that would retune it
    // mid-sweep has to be turned away rather than queued.
    g_config.frequency = 433.82f;

    MeterReader reader = makeReader();
    reader.performFrequencyScan();
    TEST_ASSERT_TRUE(reader.isScanInProgress());

    const int before = (int)fakeRadio().calls.size();

    reader.performFrequencyScan(); // Second press of the scan button
    reader.triggerReading(false);  // Manual read while the sweep is running

    TEST_ASSERT_EQUAL(before, (int)fakeRadio().calls.size());
    TEST_ASSERT_TRUE(FrequencyManager::isScanInProgress());

    drainFrequencyScan(reader);
}

void test_reset_frequency_offset_clears_storage_and_retunes(void)
{
    StorageAbstraction::saveFloat("freq_offset", 0.030f, 0xABCD);
    g_config.frequency = 433.82f;

    MeterReader reader = makeReader();
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 433.85f, fakeRadio().lastInitFrequency());

    reader.resetFrequencyOffset();

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 433.82f, fakeRadio().lastInitFrequency());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, StorageAbstraction::loadFloat("freq_offset", 99.0f, 0xABCD));
    TEST_ASSERT_FALSE(g_publisher.frequencyOffsets.empty());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, g_publisher.frequencyOffsets.back());
}

void test_successful_reads_feed_adaptive_frequency_tracking(void)
{
    // Ten reads each reporting the same sizeable error must move the offset by
    // half the average error, and persist it.
    g_config.frequency = 433.82f;
    fakeRadio().responses.push_back(FakeRadio::success(1000, 40));

    MeterReader reader = makeReader();
    for (int i = 0; i < 10; i++)
    {
        reader.triggerReading(false);
    }

    const float expected = 10 * (40 * 0.001587f) / 10 * 0.5f;
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, expected, FrequencyManager::getOffset());
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, expected,
                             StorageAbstraction::loadFloat("freq_offset", 0.0f, 0xABCD));
}

void test_small_frequency_errors_do_not_move_the_offset(void)
{
    // 1 LSB is about 1.59 kHz, below the 2 kHz adaptation threshold.
    fakeRadio().responses.push_back(FakeRadio::success(1000, 1));

    MeterReader reader = makeReader();
    for (int i = 0; i < 10; i++)
    {
        reader.triggerReading(false);
    }

    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.0f, FrequencyManager::getOffset());
}
