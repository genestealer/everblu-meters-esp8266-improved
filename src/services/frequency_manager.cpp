/**
 * @file frequency_manager.cpp
 * @brief Implementation of frequency management and calibration
 */

#include "frequency_manager.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include "storage_abstraction.h"
#include <cmath>
#include <cstring>
#if defined(ESP32)
#include <esp_task_wdt.h>
#endif

FrequencyManager::Calibration FrequencyManager::s_defaultCalibration;
FrequencyManager::Calibration *FrequencyManager::s_calibration = &FrequencyManager::s_defaultCalibration;
FrequencyManager::ScanState FrequencyManager::s_scan{};
FrequencyManager::ScanOutcome FrequencyManager::s_lastScanOutcome = ScanOutcome::None;
bool FrequencyManager::s_scanCancelRequested = false;
RadioInitCallback FrequencyManager::s_radioInitCallback = nullptr;
MeterReadCallback FrequencyManager::s_meterReadCallback = nullptr;

FrequencyManager::Calibration::~Calibration() { FrequencyManager::releaseCalibration(*this); }

// Releasing the calibration that owns a running scan must also end the scan: the
// state machine would otherwise keep stepping against a destroyed profile.
void FrequencyManager::releaseCalibration(Calibration &calibration)
{
    if (s_calibration != &calibration) return;
    if (isScanInProgress())
    {
        g_echo_debug_quiet = s_scan.quietPrevious;
        s_scan.phase = ScanPhase::Idle;
        s_lastScanOutcome = ScanOutcome::Aborted;
    }
    s_calibration = &s_defaultCalibration;
}

// One CC1101 is shared by every meter, so a scan keeps ownership of the radio until
// it finishes. Refusing the switch is what stops a second meter retuning mid-sweep.
bool FrequencyManager::activateCalibration(Calibration &calibration)
{
    if (isScanInProgress() && s_calibration != &calibration) return false;
    s_calibration = &calibration;
    return true;
}

void FrequencyManager::feedWatchdog()
{
#if defined(ESP8266)
    ESP.wdtFeed();
#elif defined(ESP32)
    esp_task_wdt_reset();
    yield();
#endif
}

bool FrequencyManager::validateCallbacks() { return s_radioInitCallback && s_meterReadCallback; }
void FrequencyManager::reportPhase(const char *message)
{
    LOG_I("everblu_meter", "%s: %s", s_calibration->storageKey, message);
    if (s_scan.statusCallback) s_scan.statusCallback("Frequency Scanning", message);
}
void FrequencyManager::setRadioInitCallback(RadioInitCallback callback) { s_radioInitCallback = callback; }
void FrequencyManager::setMeterReadCallback(MeterReadCallback callback) { s_meterReadCallback = callback; }

// Load one meter's calibration. A NaN sentinel distinguishes "nothing stored" from a
// genuinely stored 0.0, so a deliberate zero offset is not mistaken for uncalibrated.
void FrequencyManager::initialiseCalibration(Calibration &calibration, float baseFrequency, const char *storageKey)
{
    calibration.baseFrequency = baseFrequency;
    snprintf(calibration.storageKey, sizeof(calibration.storageKey), "%s", storageKey);
    StorageAbstraction::begin();
    float loaded = StorageAbstraction::loadFloat(calibration.storageKey, NAN, STORAGE_MAGIC, MIN_OFFSET, MAX_OFFSET);
    calibration.hasStored = std::isfinite(loaded);
    calibration.offset = calibration.hasStored ? loaded : 0.0f;
    calibration.successfulReads = 0;
    calibration.cumulativeError = 0.0f;
    LOG_I("everblu_meter", "Calibration %s: %s, offset %.3f kHz, tuned %.6f MHz",
          calibration.storageKey, calibration.hasStored ? "restored" : "not stored",
          calibration.offset * 1000.0f, calibration.baseFrequency + calibration.offset);
}

float FrequencyManager::begin(float baseFrequency)
{
    if (!validateCallbacks() || !activateCalibration(s_defaultCalibration)) return 0.0f;
    initialiseCalibration(s_defaultCalibration, baseFrequency, "freq_offset");
    return getOffset();
}

float FrequencyManager::getOffset() { return s_calibration->offset; }
void FrequencyManager::setOffset(float offset) { s_calibration->offset = offset; }
float FrequencyManager::getBaseFrequency() { return s_calibration->baseFrequency; }
float FrequencyManager::getTunedFrequency() { return getBaseFrequency() + getOffset(); }
void FrequencyManager::setAutoScanEnabled(bool enabled) { s_calibration->autoScan = enabled; }
void FrequencyManager::setAdaptiveThreshold(int threshold) { s_calibration->adaptiveThreshold = threshold > 0 ? threshold : 1; }
bool FrequencyManager::shouldPerformAutoScan() { return s_calibration->autoScan && !s_calibration->hasStored; }

// Range-checked so a corrupt or out-of-range value can never be written; hasStored is
// only set on a confirmed write, which is what arms the issue #104 quality guard.
void FrequencyManager::saveFrequencyOffset(float offset)
{
    if (!std::isfinite(offset) || offset < MIN_OFFSET || offset > MAX_OFFSET) return;
    if (StorageAbstraction::saveFloat(s_calibration->storageKey, offset, STORAGE_MAGIC))
    {
        s_calibration->offset = offset;
        s_calibration->hasStored = true;
    }
}

bool FrequencyManager::clearCalibration()
{
    StorageAbstraction::begin();
    // An absent key is already the wanted state; ESP32 Preferences::remove() and the
    // host fake both report false for one, so only a refused erase counts as failure.
    if (StorageAbstraction::hasKey(s_calibration->storageKey) &&
        !StorageAbstraction::clearKey(s_calibration->storageKey))
        return false;
    s_calibration->offset = 0.0f;
    s_calibration->hasStored = false;
    resetAdaptiveTracking();
    return true;
}

float FrequencyManager::loadFrequencyOffset()
{
    return StorageAbstraction::loadFloat(s_calibration->storageKey, 0.0f, STORAGE_MAGIC, MIN_OFFSET, MAX_OFFSET);
}

void FrequencyManager::performDeepFrequencyScan(float range, float step, StatusCallback callback)
{
    if (isScanInProgress()) return;
    // Blocking convenience wrapper: drives the state machine to completion here.
    // Hosts with a cooperative main loop should call beginDeepFrequencyScan() and
    // pump loopScan() instead, so a Stop request can be delivered mid-scan (#133).
    beginDeepFrequencyScan(range, step, callback);
    while (isScanInProgress())
    {
        loopScan();
        feedWatchdog();
        delay(0); // service the Wi-Fi/TCP stack between steps
    }
}

// Convert an MHz window into whole CC1101 frequency words. Working in register units
// keeps every step on a distinct tunable frequency and stops float accumulation from
// drifting the sweep. The window is clamped to the persistable offset range.
void FrequencyManager::startSweep(float centre, float range, float step)
{
    float low = fmaxf(centre - range, getBaseFrequency() + MIN_OFFSET);
    float high = fminf(centre + range, getBaseFrequency() + MAX_OFFSET);
    s_scan.start = (int32_t)ceilf(low / CC1101_MIN_STEP_MHZ);
    s_scan.end = (int32_t)floorf(high / CC1101_MIN_STEP_MHZ);
    s_scan.current = s_scan.start;
    s_scan.step = (int32_t)lroundf(step / CC1101_MIN_STEP_MHZ);
    if (s_scan.step < 1) s_scan.step = 1;
    s_scan.phase = ScanPhase::Acquire;
    LOG_I("everblu_meter", "Acquisition %.6f - %.6f MHz, %.3f kHz steps",
          s_scan.start * CC1101_MIN_STEP_MHZ, s_scan.end * CC1101_MIN_STEP_MHZ,
          s_scan.step * CC1101_MIN_STEP_MHZ * 1000.0f);
}

void FrequencyManager::beginDeepFrequencyScan(float range, float step, StatusCallback callback)
{
    if (isScanInProgress()) return;
    s_lastScanOutcome = ScanOutcome::None;
    if (!validateCallbacks() || !std::isfinite(range) || !std::isfinite(step) || range <= 0 || step <= 0)
    {
        s_lastScanOutcome = ScanOutcome::Aborted;
        return;
    }
    s_scan = ScanState{};
    s_scan.statusCallback = callback;
    // Snapshot the current known-good offset so the quality guard can avoid
    // regressing a good calibration (issue #104), and so any failure can restore it.
    s_scan.previousOffset = getOffset();
    // Suppress the verbose per-attempt radio/meter logging for the whole scan; each
    // step is a full read whose detailed output is noise here. An RAII guard cannot
    // span loop iterations, so the previous value is restored by finishScan().
    s_scan.quietPrevious = g_echo_debug_quiet;
    g_echo_debug_quiet = true;
    s_scanCancelRequested = false;
    // A coarse first pass may step over a narrow response, so allow exactly one finer
    // retry. Starting fine already means there is nothing finer to fall back to.
    s_scan.finerFallback = step > 0.0025f;
    resetAdaptiveTracking();
    startSweep(getBaseFrequency(), range, step);
    reportPhase("Wide acquisition");
}

// Recovery starts where the meter was last heard rather than at the configured base,
// because ordinary drift is small. expandOnMiss widens to the full range if that
// local window is empty, so larger drift still recovers without manual intervention.
void FrequencyManager::beginRecoveryScan(StatusCallback callback)
{
    if (isScanInProgress()) return;
    beginDeepFrequencyScan(0.020f, 0.001f, callback);
    if (!isScanInProgress()) return;
    s_scan.expandOnMiss = true;
    startSweep(getTunedFrequency(), 0.020f, 0.001f);
    reportPhase("Local acquisition");
}

bool FrequencyManager::isScanInProgress() { return s_scan.phase != ScanPhase::Idle; }
FrequencyManager::ScanOutcome FrequencyManager::lastScanOutcome() { return s_lastScanOutcome; }
void FrequencyManager::requestScanCancel() { s_scanCancelRequested = true; }

// Single exit point for the scan. Anything other than a verified result puts the
// radio back on the calibration the scan started from, so a cancelled, empty or
// faulted scan never leaves the meter tuned somewhere it was only experimenting.
void FrequencyManager::finishScan(ScanOutcome outcome, const char *message)
{
    if (outcome != ScanOutcome::Found)
    {
        s_calibration->offset = s_scan.previousOffset;
        if (!s_radioInitCallback(getTunedFrequency()))
        {
            outcome = ScanOutcome::Aborted;
            message = "Radio fault while restoring tuning";
        }
    }
    g_echo_debug_quiet = s_scan.quietPrevious;
    s_scan.phase = ScanPhase::Idle;
    s_scanCancelRequested = false;
    s_lastScanOutcome = outcome;
    LOG_I("everblu_meter", "%s: %s, tuning %.6f MHz", s_calibration->storageKey, message, getTunedFrequency());
    if (s_scan.statusCallback)
        s_scan.statusCallback(outcome == ScanOutcome::Aborted ? "Error" : "Idle", message);
}

// One radio transaction. A failed retune is a hardware fault rather than an RF miss,
// so it aborts the scan instead of being recorded as a frequency that did not answer.
bool FrequencyManager::readAt(float frequency, tmeter_data &data)
{
    if (!s_radioInitCallback(frequency))
    {
        finishScan(ScanOutcome::Aborted, "Radio not responding - scan aborted");
        return false;
    }
    delay(50);
    data = s_meterReadCallback();
    LOG_I("everblu_meter", "Scan %.6f MHz: reads=%d RSSI=%d FREQEST=%d", frequency,
          data.reads_counter, data.rssi_dbm, data.freqest);
    return true;
}

void FrequencyManager::record(Quality &quality, const tmeter_data &data)
{
    quality.attempts++;
    if (data.reads_counter > 0 && data.volume > 0)
    {
        quality.successes++;
        quality.error += abs((int)data.freqest);
    }
}

// Rank by decode reliability first and |FREQEST| only as a tie-break: a strong RSSI
// tens of kHz off the true carrier can still yield corrupted (CRC-failing) bits, so
// how often a frequency actually decodes is the better signal (issue #104).
bool FrequencyManager::better(const Quality &candidate, const Quality &previous)
{
    if (candidate.successes != previous.successes) return candidate.successes > previous.successes;
    return candidate.successes > 0 && candidate.error < previous.error;
}

// Stage 1: sweep for ANY response. Exhausting the range escalates once (local ->
// wide, then coarse -> finer) and then gives up; each flag is cleared as it is used
// so the fallbacks are strictly bounded and cannot loop.
void FrequencyManager::stepAcquire()
{
    if (s_scan.current > s_scan.end)
    {
        if (s_scan.expandOnMiss)
        {
            s_scan.expandOnMiss = false;
            s_scan.finerFallback = true;
            LOG_I("everblu_meter", "Local scan empty - starting wide acquisition");
            startSweep(getBaseFrequency(), 0.150f, 0.010f);
            reportPhase("Wide acquisition");
        }
        else if (s_scan.finerFallback)
        {
            s_scan.finerFallback = false;
            s_scan.step = MAP_STEP;
            s_scan.current = s_scan.start;
            reportPhase("Finer acquisition fallback");
            LOG_I("everblu_meter", "Coarse acquisition empty - retrying at %.3f kHz", MAP_STEP * CC1101_MIN_STEP_MHZ * 1000.0f);
        }
        else finishScan(ScanOutcome::NotFound, "Deep scan failed - no meter response");
        return;
    }
    tmeter_data data{};
    if (!readAt(s_scan.current * CC1101_MIN_STEP_MHZ, data)) return;
    if (data.reads_counter > 0 && data.volume > 0)
    {
        s_scan.seed = s_scan.firstHit = s_scan.lastHit = s_scan.current;
        s_scan.current -= MAP_STEP;
        s_scan.phase = ScanPhase::Lower;
        reportPhase("Bracketing lower edge");
        LOG_I("everblu_meter", "Response found - bracketing both edges");
    }
    else if (s_scan.current == s_scan.end) s_scan.current++;
    else
    {
        s_scan.current += s_scan.step;
        if (s_scan.current > s_scan.end) s_scan.current = s_scan.end;
    }
}

// Stage 2: walk out from the first response in both directions to find where the
// meter stops answering. MISS_TOLERANCE consecutive misses close an edge, so a single
// unanswered attempt is not mistaken for the boundary. Silence is checked against the
// seed first, because a meter that has gone to sleep looks exactly like an edge.
void FrequencyManager::stepBracket()
{
    bool lower = s_scan.phase == ScanPhase::Lower;
    if (s_scan.misses >= MISS_TOLERANCE)
    {
        beginProbe();
        return;
    }
    if (s_scan.current < s_scan.start || s_scan.current > s_scan.end)
    {
        closeEdge(lower);
        return;
    }
    tmeter_data data{};
    if (!readAt(s_scan.current * CC1101_MIN_STEP_MHZ, data)) return;
    if (data.reads_counter > 0 && data.volume > 0)
    {
        if (lower) s_scan.firstHit = s_scan.current;
        else s_scan.lastHit = s_scan.current;
        s_scan.misses = 0;
    }
    else s_scan.misses++;
    s_scan.current += lower ? -MAP_STEP : MAP_STEP;
}

// Declare one bracket edge found and move on: lower hands over to the upper walk,
// upper opens the fine sweep.
void FrequencyManager::closeEdge(bool lower)
{
    s_scan.misses = 0;
    if (lower)
    {
        s_scan.current = s_scan.seed + MAP_STEP;
        s_scan.phase = ScanPhase::Upper;
        reportPhase("Bracketing upper edge");
        return;
    }
    s_scan.zoomStart = s_scan.firstHit - MAP_STEP;
    if (s_scan.zoomStart < s_scan.start) s_scan.zoomStart = s_scan.start;
    s_scan.zoomEnd = s_scan.lastHit + MAP_STEP;
    if (s_scan.zoomEnd > s_scan.end) s_scan.zoomEnd = s_scan.end;
    s_scan.current = s_scan.zoomStart;
    s_scan.zoomMisses = 0;
    s_scan.phase = ScanPhase::Zoom;
    reportPhase("Fine window scan");
    LOG_I("everblu_meter", "Window %.6f - %.6f MHz; full fine sweep at %.3f kHz",
          s_scan.zoomStart * CC1101_MIN_STEP_MHZ, s_scan.zoomEnd * CC1101_MIN_STEP_MHZ,
          ZOOM_STEP * CC1101_MIN_STEP_MHZ * 1000.0f);
}

// A run of misses means either the wrong frequency or a meter that has stopped
// answering (they duty-cycle, and a long sweep is itself enough to quieten one).
// Re-reading the seed, which is known to answer, tells the two apart; without it a
// sleeping meter is mapped as hundreds of dead frequencies.
void FrequencyManager::beginProbe()
{
    s_scan.resumePhase = s_scan.phase;
    s_scan.phase = ScanPhase::Probe;
    reportPhase("Checking meter is awake");
}

void FrequencyManager::stepProbe()
{
    tmeter_data data{};
    if (!readAt(s_scan.seed * CC1101_MIN_STEP_MHZ, data)) return;
    if (!(data.reads_counter > 0 && data.volume > 0))
    {
        finishScan(ScanOutcome::Aborted, "Meter stopped answering - scan stopped, try again later");
        return;
    }
    s_scan.zoomMisses = 0;
    if (s_scan.resumePhase == ScanPhase::Zoom)
    {
        s_scan.phase = ScanPhase::Zoom;
        reportPhase("Fine window scan");
        return;
    }
    closeEdge(s_scan.resumePhase == ScanPhase::Lower);
}

// Stage 3: sample the WHOLE bracket, twice per frequency, and keep the best. Stopping
// at the first decode would lock onto the lower edge of the response window rather
// than its centre, which is what the earlier implementation did.
void FrequencyManager::stepZoom()
{
    if (s_scan.current > s_scan.zoomEnd)
    {
        if (s_scan.bestQuality.successes == 0) finishScan(ScanOutcome::NotFound, "Fine scan failed - restoring tuning");
        else
        {
            s_scan.phase = ScanPhase::VerifyCandidate;
            reportPhase("Verifying candidate");
        }
        return;
    }
    tmeter_data data{};
    if (!readAt(s_scan.current * CC1101_MIN_STEP_MHZ, data)) return;
    record(s_scan.sample, data);
    if (s_scan.sample.attempts < 2) return;
    bool tied = s_scan.sample.successes == s_scan.bestQuality.successes && s_scan.sample.error == s_scan.bestQuality.error;
    int32_t midpointTwice = s_scan.firstHit + s_scan.lastHit;
    if (better(s_scan.sample, s_scan.bestQuality) ||
        (s_scan.sample.successes > 0 && tied && abs(2 * s_scan.current - midpointTwice) < abs(2 * s_scan.best - midpointTwice)))
    {
        s_scan.best = s_scan.current;
        s_scan.bestQuality = s_scan.sample;
    }
    if (s_scan.sample.successes == 0) s_scan.zoomMisses++;
    else s_scan.zoomMisses = 0;
    s_scan.sample = Quality{};
    s_scan.current += ZOOM_STEP;
    // Advanced first, so resuming after the probe carries on rather than re-testing.
    if (s_scan.zoomMisses >= MISS_TOLERANCE) beginProbe();
}

// Stage 4: confirm before persisting. The candidate must decode at least twice out of
// three, and an existing calibration is re-measured over the same number of reads so a
// known-good offset is only replaced by something demonstrably better (issue #104).
void FrequencyManager::stepVerify()
{
    bool candidate = s_scan.phase == ScanPhase::VerifyCandidate;
    Quality &quality = candidate ? s_scan.candidateQuality : s_scan.storedQuality;
    float frequency = candidate ? s_scan.best * CC1101_MIN_STEP_MHZ : getBaseFrequency() + s_scan.previousOffset;
    if (quality.attempts < 3)
    {
        tmeter_data data{};
        if (readAt(frequency, data)) record(quality, data);
        return;
    }
    if (candidate)
    {
        if (quality.successes < 2)
        {
            finishScan(ScanOutcome::NotFound, "Candidate verification failed - restoring tuning");
            return;
        }
        s_scan.phase = s_calibration->hasStored ? ScanPhase::VerifyStored : ScanPhase::Finalise;
        s_scan.acceptCandidate = true;
        reportPhase(s_calibration->hasStored ? "Verifying stored calibration" : "Saving calibration");
    }
    else
    {
        s_scan.acceptCandidate = better(s_scan.candidateQuality, s_scan.storedQuality);
        s_scan.phase = ScanPhase::Finalise;
        reportPhase("Saving calibration");
    }
}

void FrequencyManager::loopScan()
{
    if (!isScanInProgress()) return;
    feedWatchdog();
    // Checked before every stage, so a Stop delivered while control was back in the
    // host loop is honoured at the next step boundary rather than at the end (#133).
    if (s_scanCancelRequested)
    {
        finishScan(ScanOutcome::Aborted, "Deep scan cancelled");
        return;
    }
    switch (s_scan.phase)
    {
    case ScanPhase::Acquire: stepAcquire(); break;
    case ScanPhase::Lower:
    case ScanPhase::Upper: stepBracket(); break;
    case ScanPhase::Probe: stepProbe(); break;
    case ScanPhase::Zoom: stepZoom(); break;
    case ScanPhase::VerifyCandidate:
    case ScanPhase::VerifyStored: stepVerify(); break;
    case ScanPhase::Finalise:
        if (s_scan.acceptCandidate)
        {
            float offset = s_scan.best * CC1101_MIN_STEP_MHZ - getBaseFrequency();
            if (!s_radioInitCallback(getBaseFrequency() + offset))
            {
                finishScan(ScanOutcome::Aborted, "Radio fault before saving calibration");
                break;
            }
            if (!StorageAbstraction::saveFloat(s_calibration->storageKey, offset, STORAGE_MAGIC))
            {
                finishScan(ScanOutcome::Aborted, "Calibration save failed");
                break;
            }
            s_calibration->offset = offset;
            s_calibration->hasStored = true;
        }
        finishScan(ScanOutcome::Found, "Deep scan complete - verified calibration");
        break;
    default: break;
    }
}

void FrequencyManager::resetAdaptiveTracking()
{
    s_calibration->cumulativeError = 0;
    s_calibration->successfulReads = 0;
}

// FREQEST is a two's complement estimate of the residual carrier error, ~1.59 kHz per
// LSB on a 26 MHz crystal. Only half the measured average is applied, and only once it
// exceeds 2 kHz, so normal measurement noise does not walk the offset around.
void FrequencyManager::adaptiveFrequencyTracking(int8_t freqest)
{
    s_calibration->cumulativeError += freqest * FREQEST_TO_MHZ;
    if (++s_calibration->successfulReads < s_calibration->adaptiveThreshold) return;
    float average = s_calibration->cumulativeError / s_calibration->successfulReads;
    if (fabsf(average * 1000.0f) > 2.0f)
    {
        saveFrequencyOffset(getOffset() + average * 0.5f);
        if (s_radioInitCallback) s_radioInitCallback(getTunedFrequency());
    }
    resetAdaptiveTracking();
}
