#include <unity.h>
#include <cmath>
#include <cstring>
#include <set>
#include "native_fakes.h"
#include "services/frequency_manager.h"

namespace
{
    constexpr float BASE = 433.82f;
    constexpr float STEP = 26.0f / 65536.0f;
    constexpr const char *REFINE_PHASE = "Refining around first response";
    const char *targetPhase = nullptr;
    int phaseAction = 0;
    bool targetVisited = false;
    bool refining = false;
    int carrierReads = 0;
    void scanProgress(const char *, const char *message)
    {
        refining |= strcmp(message, REFINE_PHASE) == 0;
        if (targetPhase == nullptr || strcmp(message, targetPhase) != 0) return;
        targetVisited = true;
        if (phaseAction == 1) FrequencyManager::requestScanCancel();
        if (phaseAction == 2) fakeRadio().initSucceeds = false;
        if (phaseAction == 3) fakeRadio().carrierFrequency = 0;
    }
    tmeter_data readScanMeter() { return get_meter_data_for_meter(20, 257750); }

    // Answer only once in two at the exact carrier, and both times everywhere else, so
    // the frequency with the lowest |FREQEST| is not the one that decodes reliably.
    tmeter_data readWithDropouts()
    {
        if (refining && fabsf(fakeRadio().lastInitFrequency() - fakeRadio().carrierFrequency) < 0.0015f &&
            ++carrierReads % 2 == 1)
            return FakeRadio::failure(ReadFailure::NoReply);
        return readScanMeter();
    }

    // Model a meter that stops answering for a stretch of the refinement and comes back.
    int refineReads = 0;
    int quietUntil = 0;
    bool quietSpellUsed = false;
    void quietSpellScanProgress(const char *, const char *message)
    {
        if (quietSpellUsed || strcmp(message, REFINE_PHASE) != 0) return;
        quietSpellUsed = true;
        quietUntil = refineReads + 6; // three frequencies at two reads each
    }
    tmeter_data readWithQuietSpell()
    {
        if (++refineReads <= quietUntil) return FakeRadio::failure(ReadFailure::NoReply);
        return readScanMeter();
    }

    // Answer once at the very first frequency tried, wherever that is, and thereafter
    // only where the carrier really is.
    tmeter_data readOneOffThenCarrier()
    {
        tmeter_data data = readScanMeter();
        if (fakeRadio().calls.size() == 1) return FakeRadio::success();
        return data;
    }

    // Answer the first read at every frequency and never on a repeat, so no frequency
    // can ever be confirmed and every acquisition step looks like a response.
    std::set<int> frequenciesTried;
    tmeter_data readNeverTwice()
    {
        readScanMeter();
        const int word = (int)lroundf(fakeRadio().lastInitFrequency() / STEP);
        if (frequenciesTried.insert(word).second) return FakeRadio::success();
        return FakeRadio::failure(ReadFailure::NoReply);
    }

    void startManager()
    {
        FrequencyManager::setRadioInitCallback(cc1101_init);
        FrequencyManager::setMeterReadCallback(readScanMeter);
        FrequencyManager::begin(BASE);
        targetPhase = nullptr;
        targetVisited = refining = false;
        carrierReads = 0;
        refineReads = quietUntil = 0;
        quietSpellUsed = false;
        frequenciesTried.clear();
    }
}

void test_staged_scan_falls_back_and_finds_a_narrow_carrier()
{
    startManager();
    const int32_t start = (int32_t)ceilf((BASE - 0.150f) / STEP);
    fakeRadio().carrierFrequency = (start + 12) * STEP;
    fakeRadio().carrierWidthMHz = STEP * 0.4f;
    FrequencyManager::performDeepFrequencyScan();
    TEST_ASSERT_TRUE(FrequencyManager::lastScanOutcome() == FrequencyManager::ScanOutcome::Found);
    TEST_ASSERT_FLOAT_WITHIN(STEP, fakeRadio().carrierFrequency, FrequencyManager::getTunedFrequency());
    int starts = 0;
    for (const auto &call : fakeRadio().calls)
        if (fabsf(call.frequency - start * STEP) < STEP * 0.1f) starts++;
    TEST_ASSERT_GREATER_OR_EQUAL(2, starts);
    FrequencyManager::begin(BASE);
    TEST_ASSERT_FLOAT_WITHIN(STEP, fakeRadio().carrierFrequency, FrequencyManager::getTunedFrequency());
}

// The response band is far wider than the tuning resolution, so the first frequency
// that answers may sit at a marginal corner of it. Refinement samples a short window
// either side rather than accepting the acquisition hit outright.
void test_staged_scan_refines_on_both_sides_of_the_first_response()
{
    startManager();
    fakeRadio().carrierFrequency = BASE + 0.060f;
    fakeRadio().carrierWidthMHz = 0.0075f;
    FrequencyManager::performDeepFrequencyScan();
    TEST_ASSERT_TRUE(FrequencyManager::lastScanOutcome() == FrequencyManager::ScanOutcome::Found);
    TEST_ASSERT_FLOAT_WITHIN(0.0075f, fakeRadio().carrierFrequency, FrequencyManager::getTunedFrequency());
    // Refinement reads each frequency twice, so a repeated frequency marks a sample.
    float lowest = 0.0f;
    float highest = 0.0f;
    for (size_t index = 1; index < fakeRadio().calls.size(); index++)
    {
        float frequency = fakeRadio().calls[index].frequency;
        if (frequency != fakeRadio().calls[index - 1].frequency) continue;
        if (lowest == 0.0f || frequency < lowest) lowest = frequency;
        if (frequency > highest) highest = frequency;
    }
    // Four steps of six frequency words each side of the first response.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 8 * 6 * STEP, highest - lowest);
}

// The real radio decodes over a band far wider than the tuning resolution, so a scan
// that tried to map that band's edges swept 171 kHz at 793 Hz: over 400 reads, around
// 25 minutes, far longer than the meter stays awake. Cost must not scale with the band.
void test_staged_scan_cost_does_not_scale_with_the_response_band()
{
    startManager();
    fakeRadio().carrierFrequency = BASE + 0.020f;
    fakeRadio().carrierWidthMHz = 0.090f;

    FrequencyManager::performDeepFrequencyScan(0.150f, 0.010f);

    TEST_ASSERT_TRUE(FrequencyManager::lastScanOutcome() == FrequencyManager::ScanOutcome::Found);
    // Acquisition walks up to the band, then a fixed nine frequencies and verification.
    TEST_ASSERT_LESS_THAN(60, (int)fakeRadio().calls.size());
}

// A response near the end of the range leaves no room for a full window either side.
// Clipping the window to the range bound used to shift the whole grid off the seed, so
// the one frequency known to answer was never sampled again and the scan gave up.
void test_staged_scan_samples_the_seed_when_the_window_is_clipped()
{
    startManager();
    const int32_t localStart = (int32_t)ceilf((BASE - 0.020f) / STEP);
    fakeRadio().carrierFrequency = (localStart + 3) * STEP;
    fakeRadio().carrierWidthMHz = STEP * 0.4f;

    FrequencyManager::beginRecoveryScan();
    while (FrequencyManager::isScanInProgress()) FrequencyManager::loopScan();

    TEST_ASSERT_TRUE(FrequencyManager::lastScanOutcome() == FrequencyManager::ScanOutcome::Found);
    TEST_ASSERT_FLOAT_WITHIN(STEP, fakeRadio().carrierFrequency, FrequencyManager::getTunedFrequency());
}

// One decode proves very little when the meter answers on its own schedule. A response
// that refinement cannot reproduce must not end a scan that still has most of its
// range untested: the real carrier here sits well above the frequency that answered.
void test_staged_scan_resumes_after_a_response_it_cannot_reproduce()
{
    startManager();
    fakeRadio().carrierFrequency = BASE + 0.060f;
    fakeRadio().carrierWidthMHz = 0.0075f;
    FrequencyManager::setMeterReadCallback(readOneOffThenCarrier);

    FrequencyManager::performDeepFrequencyScan();

    TEST_ASSERT_TRUE(FrequencyManager::lastScanOutcome() == FrequencyManager::ScanOutcome::Found);
    TEST_ASSERT_FLOAT_WITHIN(0.0075f, fakeRadio().carrierFrequency, FrequencyManager::getTunedFrequency());
}

// The counterpart bound: resuming must not let a meter that answers once and sleeps
// restart the sweep over and over, which would cost a refinement window every time.
void test_staged_scan_gives_up_after_repeated_false_starts()
{
    startManager();
    FrequencyManager::setMeterReadCallback(readNeverTwice);

    FrequencyManager::performDeepFrequencyScan(0.150f, 0.010f);

    TEST_ASSERT_TRUE(FrequencyManager::lastScanOutcome() == FrequencyManager::ScanOutcome::NotFound);
    TEST_ASSERT_EQUAL(0, fakeStorage().saveCalls);
    // Three refinement windows at most: around 22 reads each, plus acquisition.
    TEST_ASSERT_LESS_THAN(120, (int)fakeRadio().calls.size());
}

void test_calibration_profiles_keep_independent_storage_and_tracking()
{
    startManager();
    FrequencyManager::Calibration first;
    FrequencyManager::Calibration second;
    FrequencyManager::initialiseCalibration(first, BASE, "meter_first");
    FrequencyManager::initialiseCalibration(second, BASE + 0.010f, "meter_second");
    TEST_ASSERT_TRUE(FrequencyManager::activateCalibration(first));
    FrequencyManager::saveFrequencyOffset(0.120f);
    FrequencyManager::setAdaptiveThreshold(2);
    FrequencyManager::adaptiveFrequencyTracking(4);
    TEST_ASSERT_TRUE(FrequencyManager::activateCalibration(second));
    FrequencyManager::saveFrequencyOffset(-0.130f);
    FrequencyManager::setAdaptiveThreshold(3);
    FrequencyManager::adaptiveFrequencyTracking(-4);
    TEST_ASSERT_EQUAL(1, first.successfulReads);
    TEST_ASSERT_EQUAL(1, second.successfulReads);
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.120f, first.offset);
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, -0.130f, second.offset);
    FrequencyManager::initialiseCalibration(first, BASE, "meter_first");
    FrequencyManager::initialiseCalibration(second, BASE + 0.010f, "meter_second");
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.120f, first.offset);
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, -0.130f, second.offset);
    TEST_ASSERT_TRUE(FrequencyManager::activateCalibration(first));
    FrequencyManager::beginDeepFrequencyScan();
    TEST_ASSERT_FALSE(FrequencyManager::activateCalibration(second));
    FrequencyManager::requestScanCancel();
    FrequencyManager::loopScan();
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, BASE + 0.120f, fakeRadio().lastInitFrequency());
    TEST_ASSERT_TRUE(FrequencyManager::activateCalibration(second));
}

void test_staged_scan_cancels_every_phase_without_saving()
{
    const char *phases[] = {"Wide acquisition", "Finer acquisition fallback", REFINE_PHASE,
        "Verifying candidate", "Verifying stored calibration", "Saving calibration"};
    for (const char *phase : phases)
    {
        resetAllFakes();
        startManager();
        FrequencyManager::saveFrequencyOffset(-0.020f);
        fakeRadio().carrierFrequency = BASE + 0.060f;
        fakeRadio().carrierWidthMHz = 0.0075f;
        if (strcmp(phase, "Finer acquisition fallback") == 0) fakeRadio().carrierFrequency = 0;
        targetPhase = phase;
        phaseAction = 1;
        FrequencyManager::performDeepFrequencyScan(0.150f, 0.010f, scanProgress);
        TEST_ASSERT_TRUE_MESSAGE(targetVisited, phase);
        TEST_ASSERT_TRUE(FrequencyManager::lastScanOutcome() == FrequencyManager::ScanOutcome::Aborted);
        TEST_ASSERT_EQUAL(1, fakeStorage().saveCalls);
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, BASE - 0.020f, fakeRadio().lastInitFrequency());
    }
}

void test_staged_scan_radio_faults_never_save_candidates()
{
    const char *phases[] = {"Wide acquisition", REFINE_PHASE, "Verifying candidate",
        "Verifying stored calibration", "Saving calibration"};
    for (const char *phase : phases)
    {
        resetAllFakes();
        startManager();
        FrequencyManager::saveFrequencyOffset(-0.020f);
        fakeRadio().carrierFrequency = BASE + 0.060f;
        targetPhase = phase;
        phaseAction = 2;
        FrequencyManager::performDeepFrequencyScan(0.150f, 0.010f, scanProgress);
        TEST_ASSERT_TRUE_MESSAGE(targetVisited, phase);
        TEST_ASSERT_TRUE(FrequencyManager::lastScanOutcome() == FrequencyManager::ScanOutcome::Aborted);
        TEST_ASSERT_EQUAL(1, fakeStorage().saveCalls);
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, -0.020f, FrequencyManager::getOffset());
    }
}

void test_staged_scan_requires_verification_without_prior_calibration()
{
    startManager();
    fakeRadio().carrierFrequency = BASE + 0.060f;
    targetPhase = "Verifying candidate";
    phaseAction = 3;
    FrequencyManager::performDeepFrequencyScan(0.150f, 0.010f, scanProgress);
    TEST_ASSERT_TRUE(targetVisited);
    TEST_ASSERT_TRUE(FrequencyManager::lastScanOutcome() == FrequencyManager::ScanOutcome::NotFound);
    TEST_ASSERT_EQUAL(0, fakeStorage().saveCalls);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, BASE, fakeRadio().lastInitFrequency());
}

void test_staged_scan_prefers_reliable_decodes_over_lower_error()
{
    startManager();
    fakeRadio().carrierFrequency = BASE + 0.060f;
    fakeRadio().carrierWidthMHz = 0.0075f;
    FrequencyManager::setMeterReadCallback(readWithDropouts);
    FrequencyManager::performDeepFrequencyScan(0.150f, 0.010f, scanProgress);
    TEST_ASSERT_TRUE(refining);
    TEST_ASSERT_TRUE(FrequencyManager::lastScanOutcome() == FrequencyManager::ScanOutcome::Found);
    TEST_ASSERT_GREATER_OR_EQUAL(0.0015f, fabsf(FrequencyManager::getTunedFrequency() - fakeRadio().carrierFrequency));
    TEST_ASSERT_FLOAT_WITHIN(0.0075f, fakeRadio().carrierFrequency, FrequencyManager::getTunedFrequency());
}

// Meters duty-cycle, so a run of misses is not proof the tuning is wrong. Refinement
// samples its whole window rather than standing down part way through it.
void test_staged_scan_carries_on_through_a_quiet_spell()
{
    startManager();
    fakeRadio().carrierFrequency = BASE + 0.060f;
    fakeRadio().carrierWidthMHz = 0.0075f;
    FrequencyManager::setMeterReadCallback(readWithQuietSpell);

    FrequencyManager::performDeepFrequencyScan(0.150f, 0.010f, quietSpellScanProgress);

    TEST_ASSERT_TRUE(quietSpellUsed);
    TEST_ASSERT_TRUE(FrequencyManager::lastScanOutcome() == FrequencyManager::ScanOutcome::Found);
    TEST_ASSERT_FLOAT_WITHIN(0.0075f, fakeRadio().carrierFrequency, FrequencyManager::getTunedFrequency());
}
