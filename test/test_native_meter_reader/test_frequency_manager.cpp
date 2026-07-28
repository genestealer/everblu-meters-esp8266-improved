/**
 * @file test_frequency_manager.cpp
 * @brief Host tests for the frequency calibration engine
 *
 * The deep scan is the longest-running and least observable part of the
 * firmware: on hardware a full sweep takes minutes and its decisions are only
 * visible in the log. Against the fake radio in fakes.cpp, which answers only
 * while tuned inside a simulated response window, the same code runs in
 * milliseconds and every decision is assertable.
 */

#include <unity.h>

#include <cmath>

#include <Arduino.h>

#include "native_fakes.h"
#include "services/frequency_manager.h"

namespace
{
    constexpr float BASE_FREQ = 433.82f;
    constexpr float FREQEST_TO_MHZ = 0.001587f;

    bool radioInit(float freq) { return cc1101_init(freq); }
    tmeter_data readMeter() { return get_meter_data_for_meter(21, 123456); }

    /// Register the fake radio with FrequencyManager and initialise it.
    void beginManager()
    {
        FrequencyManager::setRadioInitCallback(radioInit);
        FrequencyManager::setMeterReadCallback(readMeter);
        FrequencyManager::begin(BASE_FREQ);
    }

    /// Place the simulated carrier at base + offsetKHz.
    void placeCarrier(float offsetKHz, float widthKHz = 10.0f)
    {
        fakeRadio().carrierFrequency = BASE_FREQ + offsetKHz / 1000.0f;
        fakeRadio().carrierWidthMHz = widthKHz / 1000.0f;
    }

    /**
     * @brief Assert that the scan ended tuned somewhere the meter answers.
     *
     * The zoom pass stops at the first frequency that decodes, which is the
     * lower edge of the response window rather than its centre, so the result
     * is only guaranteed to fall inside the window (plus the one scan step the
     * zoom starts below it). Adaptive FREQEST tracking refines it from there.
     */
    void assertLockedInsideWindow(float expectedOffsetKHz, float widthKHz, float stepKHz)
    {
        const float errorKHz = std::fabs(FrequencyManager::getOffset() * 1000.0f - expectedOffsetKHz);
        TEST_ASSERT_TRUE_MESSAGE(errorKHz <= widthKHz + stepKHz,
                                 "scan locked outside the meter's response window");
    }
}

void frequencyManagerSetUp()
{
    resetAllFakes();
}

// ---------------------------------------------------------------------------
// Initialisation and persistence
// ---------------------------------------------------------------------------

void test_freq_begin_without_callbacks_is_refused(void)
{
    // begin() must not touch storage when it cannot drive the radio, otherwise
    // a misconfigured caller would silently run with an unusable manager.
    const float result = FrequencyManager::begin(BASE_FREQ);

    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.0f, result);
    TEST_ASSERT_EQUAL(0, fakeStorage().beginCalls);
}

void test_freq_begin_starts_uncalibrated_when_storage_is_empty(void)
{
    beginManager();

    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.0f, FrequencyManager::getOffset());
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, BASE_FREQ, FrequencyManager::getTunedFrequency());
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, BASE_FREQ, FrequencyManager::getBaseFrequency());
}

void test_freq_offset_survives_a_reboot(void)
{
    beginManager();
    FrequencyManager::saveFrequencyOffset(-0.0175f);

    // Simulate a restart: static state is cleared but storage is not.
    FrequencyManager::setOffset(0.0f);
    beginManager();

    TEST_ASSERT_FLOAT_WITHIN(0.000001f, -0.0175f, FrequencyManager::getOffset());
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, BASE_FREQ - 0.0175f, FrequencyManager::getTunedFrequency());
}

void test_freq_offset_outside_the_valid_range_is_discarded(void)
{
    // A corrupted cell must not tune the radio hundreds of kHz away.
    StorageAbstraction::saveFloat("freq_offset", 5.0f, 0xABCD);
    beginManager();

    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.0f, FrequencyManager::getOffset());
}

void test_freq_offset_with_a_wrong_magic_is_discarded(void)
{
    StorageAbstraction::saveFloat("freq_offset", 0.020f, 0x1234);
    beginManager();

    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.0f, FrequencyManager::getOffset());
}

void test_freq_auto_scan_is_requested_only_while_uncalibrated(void)
{
    beginManager();
    FrequencyManager::setAutoScanEnabled(true);
    TEST_ASSERT_TRUE(FrequencyManager::shouldPerformAutoScan());

    FrequencyManager::saveFrequencyOffset(0.010f);
    TEST_ASSERT_FALSE(FrequencyManager::shouldPerformAutoScan());

    FrequencyManager::setAutoScanEnabled(false);
    FrequencyManager::setOffset(0.0f);
    TEST_ASSERT_FALSE(FrequencyManager::shouldPerformAutoScan());
}

// ---------------------------------------------------------------------------
// Deep scan
// ---------------------------------------------------------------------------

void test_freq_scan_finds_a_carrier_above_the_base_frequency(void)
{
    beginManager();
    placeCarrier(30.0f, 6.0f);

    FrequencyManager::performDeepFrequencyScan(0.050f, 0.0025f);

    TEST_ASSERT_TRUE(FrequencyManager::getOffset() > 0.0f);
    assertLockedInsideWindow(30.0f, 6.0f, 2.5f);
}

void test_freq_scan_finds_a_carrier_below_the_base_frequency(void)
{
    beginManager();
    placeCarrier(-25.0f, 6.0f);

    FrequencyManager::performDeepFrequencyScan(0.050f, 0.0025f);

    TEST_ASSERT_TRUE(FrequencyManager::getOffset() < 0.0f);
    assertLockedInsideWindow(-25.0f, 6.0f, 2.5f);
}

void test_freq_scan_persists_its_result(void)
{
    beginManager();
    placeCarrier(20.0f, 6.0f);

    FrequencyManager::performDeepFrequencyScan(0.050f, 0.0025f);

    const float saved = StorageAbstraction::loadFloat("freq_offset", 99.0f, 0xABCD);
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, FrequencyManager::getOffset(), saved);
}

void test_freq_scan_leaves_the_radio_tuned_to_the_result(void)
{
    beginManager();
    placeCarrier(15.0f, 6.0f);

    FrequencyManager::performDeepFrequencyScan(0.050f, 0.0025f);

    TEST_ASSERT_FLOAT_WITHIN(0.000001f, FrequencyManager::getTunedFrequency(),
                             fakeRadio().lastInitFrequency());
}

void test_freq_scan_without_a_carrier_keeps_the_base_frequency(void)
{
    beginManager();
    fakeRadio().carrierFrequency = 0.0f; // Meter never answers

    FrequencyManager::performDeepFrequencyScan(0.020f, 0.0025f);

    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.0f, FrequencyManager::getOffset());
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, BASE_FREQ, fakeRadio().lastInitFrequency());
    TEST_ASSERT_EQUAL(0, fakeStorage().saveCalls);
}

void test_freq_scan_aborts_when_the_radio_stops_responding(void)
{
    beginManager();
    placeCarrier(10.0f, 6.0f);
    fakeRadio().initSucceeds = false;

    FrequencyManager::performDeepFrequencyScan(0.050f, 0.0025f);

    // One failed init attempt, then out: no point sweeping a dead radio.
    TEST_ASSERT_EQUAL(0, (int)fakeRadio().calls.size());
    TEST_ASSERT_EQUAL(0, fakeStorage().saveCalls);
}

void test_freq_scan_can_be_cancelled_and_restores_the_known_good_tuning(void)
{
    beginManager();
    FrequencyManager::saveFrequencyOffset(0.012f);
    placeCarrier(30.0f, 6.0f);

    // A scan clears the cancel flag when it starts, so the request has to come
    // from inside the sweep, as it does when the user presses stop.
    fakeRadio().cancelScanAfterCalls = 3;

    FrequencyManager::performDeepFrequencyScan(0.050f, 0.0025f);

    TEST_ASSERT_EQUAL(3, (int)fakeRadio().calls.size());
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.012f, FrequencyManager::getOffset());
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, BASE_FREQ + 0.012f, fakeRadio().lastInitFrequency());
}

void test_freq_scan_keeps_a_good_stored_offset_when_the_candidate_is_worse(void)
{
    // Issue #104: a strong response tens of kHz off the true carrier must not
    // be allowed to overwrite a calibration that is already better centred.
    beginManager();
    placeCarrier(0.0f, 30.0f); // Wide window centred exactly on the base frequency
    FrequencyManager::saveFrequencyOffset(0.0f);

    FrequencyManager::performDeepFrequencyScan(0.050f, 0.0025f);

    // The window map hits the low edge first, so the candidate is off centre.
    // The quality guard compares |FREQEST| and keeps the stored offset.
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.0f, FrequencyManager::getOffset());
}

void test_freq_scan_replaces_a_stored_offset_that_no_longer_decodes(void)
{
    // The meter has drifted well away from where it used to be found.
    beginManager();
    FrequencyManager::saveFrequencyOffset(-0.040f);
    placeCarrier(35.0f, 5.0f);

    FrequencyManager::performDeepFrequencyScan(0.060f, 0.0025f);

    TEST_ASSERT_TRUE(FrequencyManager::getOffset() > 0.0f);
    assertLockedInsideWindow(35.0f, 5.0f, 2.5f);
}

void test_freq_scan_narrow_range_visits_fewer_steps_than_a_deep_sweep(void)
{
    beginManager();
    fakeRadio().carrierFrequency = 0.0f;

    FrequencyManager::performDeepFrequencyScan(0.020f, 0.001f);
    const int narrowSteps = (int)fakeRadio().calls.size();

    fakeRadio().calls.clear();
    FrequencyManager::performDeepFrequencyScan(0.150f, 0.0025f);
    const int deepSteps = (int)fakeRadio().calls.size();

    TEST_ASSERT_GREATER_THAN(narrowSteps, deepSteps);
}

// ---------------------------------------------------------------------------
// Adaptive tracking
// ---------------------------------------------------------------------------

void test_freq_adaptive_tracking_waits_for_the_threshold(void)
{
    beginManager();
    FrequencyManager::setAdaptiveThreshold(5);

    for (int i = 0; i < 4; i++)
    {
        FrequencyManager::adaptiveFrequencyTracking(50);
        TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.0f, FrequencyManager::getOffset());
    }

    FrequencyManager::adaptiveFrequencyTracking(50);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 50 * FREQEST_TO_MHZ * 0.5f, FrequencyManager::getOffset());
}

void test_freq_adaptive_tracking_applies_half_the_average_error(void)
{
    beginManager();
    FrequencyManager::setAdaptiveThreshold(4);

    // Average of 10, 20, 30, 40 is 25 LSB; half of that is applied.
    FrequencyManager::adaptiveFrequencyTracking(10);
    FrequencyManager::adaptiveFrequencyTracking(20);
    FrequencyManager::adaptiveFrequencyTracking(30);
    FrequencyManager::adaptiveFrequencyTracking(40);

    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 25 * FREQEST_TO_MHZ * 0.5f, FrequencyManager::getOffset());
}

void test_freq_adaptive_tracking_cancels_symmetric_noise(void)
{
    beginManager();
    FrequencyManager::setAdaptiveThreshold(4);

    FrequencyManager::adaptiveFrequencyTracking(30);
    FrequencyManager::adaptiveFrequencyTracking(-30);
    FrequencyManager::adaptiveFrequencyTracking(25);
    FrequencyManager::adaptiveFrequencyTracking(-25);

    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.0f, FrequencyManager::getOffset());
}

void test_freq_adaptive_tracking_corrects_downwards(void)
{
    beginManager();
    FrequencyManager::setAdaptiveThreshold(2);

    FrequencyManager::adaptiveFrequencyTracking(-60);
    FrequencyManager::adaptiveFrequencyTracking(-60);

    TEST_ASSERT_FLOAT_WITHIN(0.0005f, -60 * FREQEST_TO_MHZ * 0.5f, FrequencyManager::getOffset());
    TEST_ASSERT_TRUE(FrequencyManager::getOffset() < 0.0f);
}

void test_freq_adaptive_tracking_retunes_and_saves_after_adjusting(void)
{
    beginManager();
    FrequencyManager::setAdaptiveThreshold(2);
    fakeRadio().initFrequencies.clear();

    FrequencyManager::adaptiveFrequencyTracking(40);
    FrequencyManager::adaptiveFrequencyTracking(40);

    TEST_ASSERT_FLOAT_WITHIN(0.000001f, FrequencyManager::getTunedFrequency(),
                             fakeRadio().lastInitFrequency());
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, FrequencyManager::getOffset(),
                             StorageAbstraction::loadFloat("freq_offset", 99.0f, 0xABCD));
}

void test_freq_reset_adaptive_tracking_discards_the_accumulator(void)
{
    beginManager();
    FrequencyManager::setAdaptiveThreshold(3);

    FrequencyManager::adaptiveFrequencyTracking(60);
    FrequencyManager::adaptiveFrequencyTracking(60);
    FrequencyManager::resetAdaptiveTracking();
    FrequencyManager::adaptiveFrequencyTracking(60);

    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.0f, FrequencyManager::getOffset());
}

void test_freq_scan_result_is_a_plain_tmeter_data_by_value(void)
{
    // PR #138: a divergent local copy of tmeter_data in frequency_manager.h
    // undersized the caller's return slot and corrupted the stack during a
    // scan. Reading a full struct back through the scan callback path and
    // checking every field guards against that regressing.
    beginManager();

    tmeter_data probe = FakeRadio::success(555555, 77);
    probe.history_available = true;
    for (int i = 0; i < 13; i++)
    {
        probe.history[i] = (uint32_t)(0xA0000000u + (uint32_t)i);
    }
    snprintf(probe.meter_time, sizeof(probe.meter_time), "2026-04-27 09:59:49");
    snprintf(probe.meter_type, sizeof(probe.meter_type), "133290AL02");
    fakeRadio().responses.push_back(probe);

    tmeter_data result = readMeter();

    TEST_ASSERT_EQUAL_INT(555555, result.volume);
    TEST_ASSERT_EQUAL_INT8(77, result.freqest);
    TEST_ASSERT_TRUE(result.history_available);
    for (int i = 0; i < 13; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(0xA0000000u + (uint32_t)i, result.history[i]);
    }
    TEST_ASSERT_EQUAL_STRING("2026-04-27 09:59:49", result.meter_time);
    TEST_ASSERT_EQUAL_STRING("133290AL02", result.meter_type);
}
