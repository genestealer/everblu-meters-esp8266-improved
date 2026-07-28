/**
 * @file frequency_manager.cpp
 * @brief Implementation of frequency management and calibration
 */

#include "frequency_manager.h"
#include "logging.h"
#include "utils.h"
#include "storage_abstraction.h"
#if defined(ESP32)
#include <esp_task_wdt.h>
#endif

// Static member initialization
float FrequencyManager::s_baseFrequency = 0.0;
float FrequencyManager::s_storedOffset = 0.0;
bool FrequencyManager::s_autoScanEnabled = true;
bool FrequencyManager::s_hasStoredCalibration = false;
volatile bool FrequencyManager::s_scanCancelRequested = false;
int FrequencyManager::s_adaptiveThreshold = 10;
int FrequencyManager::s_successfulReadsCount = 0;
float FrequencyManager::s_cumulativeFreqError = 0.0;
FrequencyManager::ScanState FrequencyManager::s_scan = {};

// Callback pointers (must be set before use)
RadioInitCallback FrequencyManager::s_radioInitCallback = nullptr;
MeterReadCallback FrequencyManager::s_meterReadCallback = nullptr;

// Cross-platform watchdog helper
void FrequencyManager::feedWatchdog()
{
#if defined(ESP8266)
    ESP.wdtFeed();
#elif defined(ESP32)
    esp_task_wdt_reset();
    yield();
#endif
}

// Validate that required callbacks are set
bool FrequencyManager::validateCallbacks()
{
    if (!s_radioInitCallback)
    {
        TS_PRINTLN("[ERROR] Radio init callback not set. Call setRadioInitCallback() first!");
        return false;
    }
    if (!s_meterReadCallback)
    {
        TS_PRINTLN("[ERROR] Meter read callback not set. Call setMeterReadCallback() first!");
        return false;
    }
    return true;
}

// Callback setters
void FrequencyManager::setRadioInitCallback(RadioInitCallback callback)
{
    s_radioInitCallback = callback;
    TS_PRINTLN("[FREQ] FrequencyManager: Radio init callback registered");
}

void FrequencyManager::setMeterReadCallback(MeterReadCallback callback)
{
    s_meterReadCallback = callback;
    TS_PRINTLN("[FREQ] FrequencyManager: Meter read callback registered");
}

float FrequencyManager::begin(float baseFrequency)
{
    s_baseFrequency = baseFrequency;

    // Validate callbacks are set
    if (!validateCallbacks())
    {
        TS_PRINTLN("[ERROR] FrequencyManager::begin() failed - callbacks not configured!");
        return 0.0;
    }

    // Initialize storage
    StorageAbstraction::begin();

    // Load the persisted offset using a NaN sentinel as the "not found" default so a
    // genuinely stored value of 0.0 can be distinguished from "nothing saved". This
    // gives an unambiguous boot-time confirmation of whether calibration survived a reboot.
    float loaded = StorageAbstraction::loadFloat(STORAGE_KEY, NAN, STORAGE_MAGIC, MIN_OFFSET, MAX_OFFSET);
    bool persisted = !isnan(loaded);
    s_storedOffset = persisted ? loaded : 0.0f;
    s_hasStoredCalibration = persisted;

    if (persisted)
    {
        LOG_I("everblu_meter",
              "Frequency calibration RESTORED from storage: offset %.3f kHz (tuned %.6f MHz)",
              s_storedOffset * 1000.0, s_baseFrequency + s_storedOffset);
    }
    else
    {
        LOG_W("everblu_meter",
              "No frequency calibration stored - using default 0.000 kHz "
              "(run a Deep Frequency Scan to calibrate the radio)");
    }

    LOG_I("everblu_meter", "Initialized: base=%.6f MHz, offset=%.6f MHz",
          s_baseFrequency, s_storedOffset);

    return s_storedOffset;
}

float FrequencyManager::getOffset()
{
    return s_storedOffset;
}

void FrequencyManager::setOffset(float offset)
{
    s_storedOffset = offset;
}

float FrequencyManager::getBaseFrequency()
{
    return s_baseFrequency;
}

float FrequencyManager::getTunedFrequency()
{
    return s_baseFrequency + s_storedOffset;
}

void FrequencyManager::saveFrequencyOffset(float offset)
{
    StorageAbstraction::saveFloat(STORAGE_KEY, offset, STORAGE_MAGIC);
    s_storedOffset = offset;
    s_hasStoredCalibration = true; // A real value is now persisted; quality guard is active

    LOG_I("everblu_meter", "Frequency offset %.3f kHz saved", offset * 1000.0);
}

float FrequencyManager::loadFrequencyOffset()
{
    float offset = StorageAbstraction::loadFloat(STORAGE_KEY, 0.0, STORAGE_MAGIC, MIN_OFFSET, MAX_OFFSET);

    if (offset == 0.0)
    {
        LOG_I("everblu_meter", "No valid frequency offset found in storage");
    }

    return offset;
}

void FrequencyManager::performDeepFrequencyScan(float scanRangeMHz, float scanStepMHz, void (*statusCallback)(const char *, const char *))
{
    // Blocking convenience wrapper: drives the state machine to completion here.
    // Hosts with a cooperative main loop should call beginDeepFrequencyScan() and
    // pump loopScan() instead, so a Stop request can be delivered mid-scan (#133).
    beginDeepFrequencyScan(scanRangeMHz, scanStepMHz, statusCallback);

    while (isScanInProgress())
    {
        loopScan();
        feedWatchdog();
        delay(0); // service the Wi-Fi/TCP stack between steps
    }
}

void FrequencyManager::beginDeepFrequencyScan(float scanRangeMHz, float scanStepMHz, void (*statusCallback)(const char *, const char *))
{
    if (!validateCallbacks())
    {
        TS_PRINTLN("[ERROR] Deep scan aborted - callbacks not configured!");
        return;
    }

    if (isScanInProgress())
    {
        LOG_W("everblu_meter", "Deep scan already running - ignoring new scan request");
        return;
    }

    TS_PRINTLN("[FREQ] Performing Deep frequency scan...");

    // Fresh scan starts uncancelled. requestScanCancel() sets this flag; loopScan()
    // checks it and bails at the next step boundary.
    s_scanCancelRequested = false;

    // Reset adaptive tracking so the new offset has a chance to stabilize
    resetAdaptiveTracking();

    s_scan = ScanState{};
    s_scan.statusCallback = statusCallback;
    s_scan.freq = s_baseFrequency - scanRangeMHz;
    s_scan.scanEnd = s_baseFrequency + scanRangeMHz;
    s_scan.step = scanStepMHz;
    s_scan.firstHitFreq = -1.0f;
    s_scan.lastHitFreq = -1.0f;
    s_scan.bestRSSI = -120;
    s_scan.consecutiveMisses = 0;

    // Snapshot the current known-good offset so the quality guard can avoid
    // regressing a good calibration (issue #104).
    s_scan.previousOffset = s_storedOffset;

    // Suppress the verbose per-attempt radio/meter read logging for the whole
    // scan. Each frequency step performs a full read sequence whose detailed
    // output is irrelevant noise here; high-level scan progress (LOG_*) remains.
    // An RAII guard cannot span loop iterations now the scan is stepped, so the
    // previous value is saved here and restored by finishScan().
    s_scan.quietPrevious = g_echo_debug_quiet;
    g_echo_debug_quiet = true;

    s_scan.phase = ScanPhase::WindowMap;

    int deepStepCount = (int)roundf((s_scan.scanEnd - s_scan.freq) / s_scan.step) + 1;
    int deepEstSecs = deepStepCount * 3; // ~3 s per step (full radio TX+RX cycle)
    LOG_I("everblu_meter", "Deep scan from %.6f to %.6f MHz (%d steps, ~%d s / ~%d min)",
          s_scan.freq, s_scan.scanEnd, deepStepCount, deepEstSecs, (deepEstSecs + 30) / 60);

    if (statusCallback)
    {
        statusCallback("Frequency Scanning", "Performing Deep frequency scan");
    }
}

bool FrequencyManager::isScanInProgress()
{
    return s_scan.phase != ScanPhase::Idle;
}

// Return the scan machine to idle. Pass a null state to skip the status callback
// (used where the caller has already reported the outcome).
void FrequencyManager::finishScan(const char *state, const char *message)
{
    g_echo_debug_quiet = s_scan.quietPrevious;
    s_scan.phase = ScanPhase::Idle;
    s_scanCancelRequested = false;

    if (state != nullptr && s_scan.statusCallback)
    {
        s_scan.statusCallback(state, message);
    }
}

void FrequencyManager::loopScan()
{
    if (s_scan.phase == ScanPhase::Idle)
        return;

    feedWatchdog();

    // Reachable now that control returns to the host loop between steps: the API
    // has had a chance to parse an incoming Stop command and set the flag (#133).
    if (s_scanCancelRequested)
    {
        LOG_W("everblu_meter", "Deep scan cancelled by user");
        s_radioInitCallback(s_baseFrequency + s_storedOffset); // restore known-good tuning
        finishScan("Idle", "Deep scan cancelled");
        return;
    }

    switch (s_scan.phase)
    {
    case ScanPhase::WindowMap:
        stepWindowMap();
        break;
    case ScanPhase::Zoom:
        stepZoom();
        break;
    case ScanPhase::VerifyCandidate:
        stepVerifyCandidate();
        break;
    case ScanPhase::VerifyStored:
        stepVerifyStored();
        break;
    default:
        finishScan(nullptr, nullptr);
        break;
    }
}

// Phase 1: walk the scan range to discover the full response window. Continue past
// the first hit until MISS_TOLERANCE consecutive misses, mapping both the start and
// end of the carrier response band before zooming. One frequency per call.
void FrequencyManager::stepWindowMap()
{
    if (s_scan.freq > s_scan.scanEnd)
    {
        beginZoomOrFail();
        return;
    }

    if (!s_radioInitCallback(s_scan.freq))
    {
        LOG_E("everblu_meter", "Radio not responding - aborting Deep scan");
        LOG_E("everblu_meter", "Check: 1) Wiring connections 2) 3.3V power supply 3) SPI pins");
        finishScan("Error", "[ERROR] Radio not responding - cannot scan");
        return;
    }

    delay(100);

    struct tmeter_data test_data = s_meterReadCallback();

    LOG_I("everblu_meter", "Freq %.6f MHz: RSSI=%d dBm, reads=%d",
          s_scan.freq, test_data.rssi_dbm, test_data.reads_counter);

    if (test_data.reads_counter > 0)
    {
        if (s_scan.firstHitFreq < 0.0f)
        {
            s_scan.firstHitFreq = s_scan.freq;
            LOG_I("everblu_meter", "Window start: %.6f MHz", s_scan.freq);
        }
        s_scan.lastHitFreq = s_scan.freq;
        if (test_data.rssi_dbm > s_scan.bestRSSI) s_scan.bestRSSI = test_data.rssi_dbm;
        s_scan.consecutiveMisses = 0;
    }
    else if (s_scan.firstHitFreq >= 0.0f)
    {
        if (++s_scan.consecutiveMisses >= MISS_TOLERANCE)
        {
            LOG_I("everblu_meter", "Window end: %.6f MHz (%d consecutive misses)",
                  s_scan.lastHitFreq, s_scan.consecutiveMisses);
            beginZoomOrFail();
            return;
        }
    }

    s_scan.freq += s_scan.step;
}

// Window map finished: set up the zoom pass, or report failure if nothing responded.
void FrequencyManager::beginZoomOrFail()
{
    if (s_scan.firstHitFreq < 0.0f)
    {
        TS_PRINTLN("[FREQ] Deep scan failed - no meter signal found!");
        TS_PRINTLN("[FREQ] Please check:");
        TS_PRINTLN("[FREQ]  1. Meter is within range (< 50m typically)");
        TS_PRINTLN("[FREQ]  2. Antenna is connected to CC1101");
        TS_PRINTLN("[FREQ]  3. Meter serial/year are correct");
        TS_PRINTLN("[FREQ]  4. Current time is within meter's wake hours");
        s_radioInitCallback(s_baseFrequency);
        finishScan("Idle", "Deep scan failed - check setup");
        return;
    }

    float windowMidFreq = (s_scan.firstHitFreq + s_scan.lastHitFreq) * 0.5f;
    float windowWidthKHz = (s_scan.lastHitFreq - s_scan.firstHitFreq) * 1000.0f;
    LOG_I("everblu_meter", "Window: %.6f - %.6f MHz (%.2f kHz wide), midpoint %.6f MHz",
          s_scan.firstHitFreq, s_scan.lastHitFreq, windowWidthKHz, windowMidFreq);

    s_scan.bestFreq = windowMidFreq;

    // Phase 2: zoom scan across the full discovered window with 4x finer steps.
    // Always runs: even when Phase 1 found only a single point, that hit may be
    // on the edge of the response band; finer steps can locate the true centre.
    // Falls back to windowMidFreq (= firstHitFreq for single-point windows) if
    // all zoom steps miss (FREQEST adaptive tracking will then refine further).
    float zoomStart = s_scan.firstHitFreq - s_scan.step;
    s_scan.zoomEnd = s_scan.lastHitFreq + s_scan.step;
    s_scan.zoomStep = s_scan.step * 0.25f;
    if (s_scan.zoomStep < CC1101_MIN_STEP_MHZ) s_scan.zoomStep = CC1101_MIN_STEP_MHZ;

    int zoomStepCount = (int)roundf((s_scan.zoomEnd - zoomStart) / s_scan.zoomStep) + 1;
    LOG_I("everblu_meter", "Zoom pass: %.6f - %.6f MHz (%d steps, %.2f kHz each)",
          zoomStart, s_scan.zoomEnd, zoomStepCount, s_scan.zoomStep * 1000.0f);

    s_scan.freq = zoomStart;
    s_scan.phase = ScanPhase::Zoom;
}

// Phase 2: one zoom frequency per call; stops at the first decode.
void FrequencyManager::stepZoom()
{
    if (s_scan.freq > s_scan.zoomEnd + s_scan.zoomStep * 0.5f)
    {
        s_scan.phase = ScanPhase::VerifyCandidate;
        return;
    }

    if (!s_radioInitCallback(s_scan.freq))
    {
        s_scan.phase = ScanPhase::VerifyCandidate;
        return;
    }

    delay(50);

    struct tmeter_data zdata = s_meterReadCallback();
    LOG_I("everblu_meter", "Zoom %.6f MHz: RSSI=%d dBm, reads=%d",
          s_scan.freq, zdata.rssi_dbm, zdata.reads_counter);

    if (zdata.reads_counter > 0)
    {
        s_scan.bestFreq = s_scan.freq;
        s_scan.bestRSSI = zdata.rssi_dbm;
        LOG_I("everblu_meter", "Zoom locked at %.6f MHz: RSSI=%d dBm", s_scan.freq, zdata.rssi_dbm);
        s_scan.phase = ScanPhase::VerifyCandidate;
        return;
    }

    s_scan.freq += s_scan.zoomStep;
}

// Post-lock verification + quality guard (issue #104): rank candidates by
// demodulation quality (smallest |FREQEST|), not RSSI, and never overwrite
// an existing known-good offset with a worse one. A strong RSSI at a
// frequency tens of kHz off the true carrier can still yield corrupted
// (CRC-failing) bits, so RSSI alone is an unreliable ranking signal.
void FrequencyManager::stepVerifyCandidate()
{
    float offset = s_scan.bestFreq - s_baseFrequency;
    LOG_I("everblu_meter", "Deep scan candidate: %.6f MHz (offset: %.6f MHz, RSSI: %d dBm)",
          s_scan.bestFreq, offset, s_scan.bestRSSI);

    s_radioInitCallback(s_scan.bestFreq);
    delay(100);

    struct tmeter_data candVerify = s_meterReadCallback();
    bool candDecoded = candVerify.reads_counter > 0;
    s_scan.candQuality = abs((int)candVerify.freqest); // smaller = better centred
    LOG_I("everblu_meter", "Verify candidate %.6f MHz: reads=%d, |FREQEST|=%d",
          s_scan.bestFreq, candVerify.reads_counter, s_scan.candQuality);

    if (!s_hasStoredCalibration)
    {
        // No prior calibration to protect: persist the scan result as-is.
        stepFinalise(true);
    }
    else if (!candDecoded)
    {
        // Candidate failed post-lock verification: keep the known-good offset.
        LOG_W("everblu_meter",
              "Candidate %.6f MHz did not verify (no decode) - keeping stored offset %.3f kHz",
              s_scan.bestFreq, s_scan.previousOffset * 1000.0);
        stepFinalise(false);
    }
    else
    {
        s_scan.phase = ScanPhase::VerifyStored;
    }
}

// Both candidate and stored offset decode: keep the better-centred one.
void FrequencyManager::stepVerifyStored()
{
    s_radioInitCallback(s_baseFrequency + s_scan.previousOffset);
    delay(100);

    struct tmeter_data prevVerify = s_meterReadCallback();
    bool prevDecoded = prevVerify.reads_counter > 0;
    int prevQuality = abs((int)prevVerify.freqest);
    LOG_I("everblu_meter", "Verify stored %.6f MHz: reads=%d, |FREQEST|=%d",
          s_baseFrequency + s_scan.previousOffset, prevVerify.reads_counter, prevQuality);

    bool acceptCandidate;
    if (!prevDecoded)
    {
        acceptCandidate = true; // stored offset no longer decodes
    }
    else
    {
        acceptCandidate = s_scan.candQuality < prevQuality; // strictly better only
    }

    if (!acceptCandidate)
    {
        LOG_I("everblu_meter",
              "Stored offset %.3f kHz (|FREQEST|=%d) is as good or better than candidate "
              "%.3f kHz (|FREQEST|=%d) - keeping stored offset",
              s_scan.previousOffset * 1000.0, prevQuality,
              (s_scan.bestFreq - s_baseFrequency) * 1000.0, s_scan.candQuality);
    }

    stepFinalise(acceptCandidate);
}

// Save (or keep) the offset, retune the radio and return the scan machine to idle.
void FrequencyManager::stepFinalise(bool acceptCandidate)
{
    if (acceptCandidate)
    {
        float offset = s_scan.bestFreq - s_baseFrequency;
        saveFrequencyOffset(offset);
        LOG_I("everblu_meter", "Deep scan complete! Saved offset %.3f kHz (tuned %.6f MHz)",
              offset * 1000.0, s_scan.bestFreq);
    }
    else
    {
        LOG_I("everblu_meter", "Deep scan complete - retained existing offset %.3f kHz",
              s_storedOffset * 1000.0);
    }

    if (s_scan.statusCallback)
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "Deep scan complete: offset %.3f kHz", s_storedOffset * 1000.0);
        s_scan.statusCallback("Idle", msg);
    }

    delay(100);
    s_radioInitCallback(s_baseFrequency + s_storedOffset);
    delay(100);
    LOG_I("everblu_meter", "Radio reinitialized with new frequency: %.6f MHz", s_baseFrequency + s_storedOffset);

    finishScan(nullptr, nullptr);
}

void FrequencyManager::adaptiveFrequencyTracking(int8_t freqest)
{
    // FREQEST is a two's complement value representing frequency offset
    // Resolution is approximately Fxosc/2^14 ≈ 1.59 kHz per LSB (for 26 MHz crystal)

    // Accumulate the frequency error
    float freqErrorMHz = (float)freqest * FREQEST_TO_MHZ;
    s_cumulativeFreqError += freqErrorMHz;
    s_successfulReadsCount++;

    LOG_I("everblu_meter", "FREQEST: %d (%.4f kHz error), cumulative: %.4f kHz over %d reads",
          freqest, freqErrorMHz * 1000, s_cumulativeFreqError * 1000, s_successfulReadsCount);

    // Only adapt after N successful reads to avoid over-correcting on noise
    if (s_successfulReadsCount >= s_adaptiveThreshold)
    {
        float avgError = s_cumulativeFreqError / s_adaptiveThreshold;

        // Only adjust if average error is significant (> 2 kHz)
        if (abs(avgError * 1000) > ADAPT_MIN_ERROR_KHZ)
        {
            LOG_I("everblu_meter", "Adaptive adjustment: average error %.4f kHz over %d reads",
                  avgError * 1000, s_adaptiveThreshold);

            // Adjust the stored offset (apply 50% of the measured error to avoid over-correction)
            float adjustment = avgError * ADAPT_CORRECTION_FACTOR;
            s_storedOffset += adjustment;

            LOG_I("everblu_meter", "Adjusting frequency offset by %.3f kHz (new offset: %.3f kHz)",
                  adjustment * 1000.0, s_storedOffset * 1000.0);

            saveFrequencyOffset(s_storedOffset);

            // Reinitialize radio with adjusted frequency
            s_radioInitCallback(s_baseFrequency + s_storedOffset);
        }
        else
        {
            LOG_I("everblu_meter", "Frequency stable (avg error %.4f kHz < %.1f kHz threshold)",
                  avgError * 1000, ADAPT_MIN_ERROR_KHZ);
        }

        // Reset accumulators
        resetAdaptiveTracking();
    }
}

void FrequencyManager::resetAdaptiveTracking()
{
    s_cumulativeFreqError = 0.0;
    s_successfulReadsCount = 0;
    LOG_I("everblu_meter", "Adaptive frequency tracking reset");
}

void FrequencyManager::requestScanCancel()
{
    s_scanCancelRequested = true;
}

bool FrequencyManager::shouldPerformAutoScan()
{
    return s_autoScanEnabled && (s_storedOffset == 0.0);
}

void FrequencyManager::setAutoScanEnabled(bool enabled)
{
    s_autoScanEnabled = enabled;
}

void FrequencyManager::setAdaptiveThreshold(int threshold)
{
    s_adaptiveThreshold = threshold;
}
