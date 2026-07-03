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
int FrequencyManager::s_adaptiveThreshold = 10;
int FrequencyManager::s_successfulReadsCount = 0;
float FrequencyManager::s_cumulativeFreqError = 0.0;

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
    TS_PRINTLN("[FREQ] Performing Deep frequency scan...");

    // Suppress the verbose per-attempt radio/meter read logging for the whole
    // scan. Each frequency step performs a full read sequence whose detailed
    // output is irrelevant noise here; high-level scan progress (LOG_*) remains.
    EchoDebugQuietGuard quietGuard;

    // Reset adaptive tracking so the new offset has a chance to stabilize
    resetAdaptiveTracking();

    if (statusCallback)
    {
        statusCallback("Frequency Scanning", "Performing Deep frequency scan");
    }

    // Phase 1: Walk the scan range to discover the full response window.
    // Continue past the first hit until MISS_TOLERANCE consecutive misses,
    // mapping both the start and end of the carrier response band before zooming.
    float firstHitFreq = -1.0f;
    float lastHitFreq  = -1.0f;
    int   bestRSSI = -120;
    int   consecutiveMisses = 0;
    const int MISS_TOLERANCE = 5;

    float scanStart = s_baseFrequency - scanRangeMHz;
    float scanEnd = s_baseFrequency + scanRangeMHz;
    float scanStep = scanStepMHz;

    int deepStepCount = (int)roundf((scanEnd - scanStart) / scanStep) + 1;
    int deepEstSecs = deepStepCount * 3; // ~3 s per step (full radio TX+RX cycle)
    LOG_I("everblu_meter", "Deep scan from %.6f to %.6f MHz (%d steps, ~%d s / ~%d min)",
          scanStart, scanEnd, deepStepCount, deepEstSecs, (deepEstSecs + 30) / 60);

    for (float freq = scanStart; freq <= scanEnd; freq += scanStep)
    {
        feedWatchdog();

        if (!s_radioInitCallback(freq))
        {
            LOG_E("everblu_meter", "Radio not responding - aborting Deep scan");
            LOG_E("everblu_meter", "Check: 1) Wiring connections 2) 3.3V power supply 3) SPI pins");
            if (statusCallback) statusCallback("Error", "[ERROR] Radio not responding - cannot scan");
            return;
        }

        delay(100);

        struct tmeter_data test_data = s_meterReadCallback();

        LOG_I("everblu_meter", "Freq %.6f MHz: RSSI=%d dBm, reads=%d",
              freq, test_data.rssi_dbm, test_data.reads_counter);

        if (test_data.reads_counter > 0)
        {
            if (firstHitFreq < 0.0f)
            {
                firstHitFreq = freq;
                LOG_I("everblu_meter", "Window start: %.6f MHz", freq);
            }
            lastHitFreq = freq;
            if (test_data.rssi_dbm > bestRSSI) bestRSSI = test_data.rssi_dbm;
            consecutiveMisses = 0;
        }
        else if (firstHitFreq >= 0.0f)
        {
            if (++consecutiveMisses >= MISS_TOLERANCE)
            {
                LOG_I("everblu_meter", "Window end: %.6f MHz (%d consecutive misses)",
                      lastHitFreq, consecutiveMisses);
                break;
            }
        }
    }

    if (firstHitFreq >= 0.0f)
    {
        float windowMidFreq = (firstHitFreq + lastHitFreq) * 0.5f;
        float windowWidthKHz = (lastHitFreq - firstHitFreq) * 1000.0f;
        LOG_I("everblu_meter", "Window: %.6f - %.6f MHz (%.2f kHz wide), midpoint %.6f MHz",
              firstHitFreq, lastHitFreq, windowWidthKHz, windowMidFreq);

        float bestFreq = windowMidFreq;

        // Phase 2: zoom scan across the full discovered window with 4x finer steps.
        // Always runs — even when Phase 1 found only a single point, that hit may be
        // on the edge of the response band; finer steps can locate the true centre.
        // Falls back to windowMidFreq (= firstHitFreq for single-point windows) if
        // all zoom steps miss (FREQEST adaptive tracking will then refine further).
        float zoomStart = firstHitFreq - scanStep;
        float zoomEnd   = lastHitFreq  + scanStep;
        // CC1101 minimum frequency step = Fxosc / 2^16 = 26 MHz / 65536 ≈ 397 Hz.
        // Steps finer than this round to the same register value, silently retesting
        // the same physical frequency. Clamp to at least 1 register step.
        const float CC1101_MIN_STEP_MHZ = 26.0f / 65536.0f / 1000.0f; // ~0.000397 MHz
        float zoomStep = scanStep * 0.25f;
        if (zoomStep < CC1101_MIN_STEP_MHZ) zoomStep = CC1101_MIN_STEP_MHZ;

        int zoomStepCount = (int)roundf((zoomEnd - zoomStart) / zoomStep) + 1;
        LOG_I("everblu_meter", "Zoom pass: %.6f - %.6f MHz (%d steps, %.2f kHz each)",
              zoomStart, zoomEnd, zoomStepCount, zoomStep * 1000.0f);

        for (float zfreq = zoomStart; zfreq <= zoomEnd + zoomStep * 0.5f; zfreq += zoomStep)
        {
            feedWatchdog();
            if (!s_radioInitCallback(zfreq)) break;
            delay(50);
            struct tmeter_data zdata = s_meterReadCallback();
            LOG_I("everblu_meter", "Zoom %.6f MHz: RSSI=%d dBm, reads=%d",
                  zfreq, zdata.rssi_dbm, zdata.reads_counter);
            if (zdata.reads_counter > 0)
            {
                bestFreq = zfreq;
                bestRSSI = zdata.rssi_dbm;
                LOG_I("everblu_meter", "Zoom locked at %.6f MHz: RSSI=%d dBm", zfreq, zdata.rssi_dbm);
                break;
            }
        }

        float offset = bestFreq - s_baseFrequency;
        LOG_I("everblu_meter", "Deep scan complete! Best frequency: %.6f MHz (offset: %.6f MHz, RSSI: %d dBm)",
              bestFreq, offset, bestRSSI);

        saveFrequencyOffset(offset);

        if (statusCallback)
        {
            char msg[128];
            snprintf(msg, sizeof(msg), "Deep scan complete: offset %.3f kHz", offset * 1000.0);
            statusCallback("Idle", msg);
        }

        delay(100);
        s_radioInitCallback(s_baseFrequency + s_storedOffset);
        delay(100);
        LOG_I("everblu_meter", "Radio reinitialized with new frequency: %.6f MHz", s_baseFrequency + s_storedOffset);
    }
    else
    {
        TS_PRINTLN("[FREQ] Deep scan failed - no meter signal found!");
        TS_PRINTLN("[FREQ] Please check:");
        TS_PRINTLN("[FREQ]  1. Meter is within range (< 50m typically)");
        TS_PRINTLN("[FREQ]  2. Antenna is connected to CC1101");
        TS_PRINTLN("[FREQ]  3. Meter serial/year are correct");
        TS_PRINTLN("[FREQ]  4. Current time is within meter's wake hours");
        if (statusCallback) statusCallback("Idle", "Deep scan failed - check setup");
        s_radioInitCallback(s_baseFrequency);
    }
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
