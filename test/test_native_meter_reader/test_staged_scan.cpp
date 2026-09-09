#include <unity.h>
#include <cmath>
#include <cstring>
#include "native_fakes.h"
#include "services/frequency_manager.h"

namespace
{
    constexpr float BASE = 433.82f;
    constexpr float STEP = 26.0f / 65536.0f;
    const char *targetPhase = nullptr;
    int phaseAction = 0;
    bool targetVisited = false;
    bool fineSampling = false;
    bool upperEdgeVisited = false;
    int sampleCount = 0;
    void scanProgress(const char *, const char *message)
    {
        fineSampling = strcmp(message, "Fine window scan") == 0;
        upperEdgeVisited |= strcmp(message, "Bracketing upper edge") == 0;
        if (targetPhase == nullptr || strcmp(message, targetPhase) != 0) return;
        targetVisited = true;
        if (phaseAction == 1) FrequencyManager::requestScanCancel();
        if (phaseAction == 2) fakeRadio().initSucceeds = false;
        if (phaseAction == 3) fakeRadio().carrierFrequency = 0;
    }
    tmeter_data readWithDropouts()
    {
        tmeter_data data = get_meter_data_for_meter(20, 257750);
        float frequency = fakeRadio().lastInitFrequency();
        if (fineSampling)
        {
            if (fabsf(frequency - fakeRadio().carrierFrequency) < 0.0015f && ++sampleCount % 2 == 1)
                return FakeRadio::failure(ReadFailure::NoReply);
        }
        else if (!upperEdgeVisited && fakeRadio().calls.size() % 3 == 0)
            return FakeRadio::failure(ReadFailure::NoReply);
        return data;
    }
    tmeter_data readScanMeter() { return get_meter_data_for_meter(20, 257750); }
    void startManager()
    {
        FrequencyManager::setRadioInitCallback(cc1101_init);
        FrequencyManager::setMeterReadCallback(readScanMeter);
        FrequencyManager::begin(BASE);
        targetPhase = nullptr;
        targetVisited = fineSampling = upperEdgeVisited = false;
        sampleCount = 0;
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

void test_staged_scan_sweeps_both_sides_and_selects_the_centre()
{
    startManager();
    fakeRadio().carrierFrequency = BASE + 0.060f;
    fakeRadio().carrierWidthMHz = 0.0075f;
    FrequencyManager::performDeepFrequencyScan();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, fakeRadio().carrierFrequency, FrequencyManager::getTunedFrequency());
    bool lowerFine = false;
    bool upperFine = false;
    for (size_t index = 1; index < fakeRadio().calls.size(); index++)
    {
        float frequency = fakeRadio().calls[index].frequency;
        if (frequency == fakeRadio().calls[index - 1].frequency)
        {
            lowerFine |= frequency < fakeRadio().carrierFrequency - 0.005f;
            upperFine |= frequency > fakeRadio().carrierFrequency + 0.005f;
        }
    }
    TEST_ASSERT_TRUE(lowerFine);
    TEST_ASSERT_TRUE(upperFine);
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
    const char *phases[] = {"Wide acquisition", "Finer acquisition fallback", "Bracketing lower edge",
        "Bracketing upper edge", "Fine window scan", "Verifying candidate", "Verifying stored calibration", "Saving calibration"};
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
    const char *phases[] = {"Wide acquisition", "Bracketing lower edge", "Bracketing upper edge",
        "Fine window scan", "Verifying candidate", "Verifying stored calibration", "Saving calibration"};
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
    TEST_ASSERT_TRUE(upperEdgeVisited);
    TEST_ASSERT_TRUE(FrequencyManager::lastScanOutcome() == FrequencyManager::ScanOutcome::Found);
    TEST_ASSERT_GREATER_OR_EQUAL(0.0015f, fabsf(FrequencyManager::getTunedFrequency() - fakeRadio().carrierFrequency));
    TEST_ASSERT_FLOAT_WITHIN(0.004f, fakeRadio().carrierFrequency, FrequencyManager::getTunedFrequency());
}
