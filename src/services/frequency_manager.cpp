/**
 * @file frequency_manager.cpp
 * @brief Implementation of frequency management and calibration
 */

#include "frequency_manager.h"
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
        Serial.println("[ERROR] Radio init callback not set. Call setRadioInitCallback() first!");
        return false;
    }
    if (!s_meterReadCallback)
    {
        Serial.println("[ERROR] Meter read callback not set. Call setMeterReadCallback() first!");
        return false;
    }
    return true;
}

// Callback setters
void FrequencyManager::setRadioInitCallback(RadioInitCallback callback)
{
    s_radioInitCallback = callback;
    Serial.println("[FREQ] FrequencyManager: Radio init callback registered");
}

void FrequencyManager::setMeterReadCallback(MeterReadCallback callback)
{
    s_meterReadCallback = callback;
    Serial.println("[FREQ] FrequencyManager: Meter read callback registered");
}

float FrequencyManager::begin(float baseFrequency)
{
    s_baseFrequency = baseFrequency;

    // Validate callbacks are set
    if (!validateCallbacks())
    {
        Serial.println("[ERROR] FrequencyManager::begin() failed - callbacks not configured!");
        return 0.0;
    }

    // Initialize storage
    StorageAbstraction::begin();

    // Load stored offset
    s_storedOffset = loadFrequencyOffset();

    Serial.printf("[FREQ] Initialized: base=%.6f MHz, offset=%.6f MHz\n",
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

    Serial.printf("[FREQ] Frequency offset %.6f MHz saved\n", offset);
}

float FrequencyManager::loadFrequencyOffset()
{
    float offset = StorageAbstraction::loadFloat(STORAGE_KEY, 0.0, STORAGE_MAGIC, MIN_OFFSET, MAX_OFFSET);

    if (offset == 0.0)
    {
        Serial.println("[FREQ] No valid frequency offset found in storage");
    }

    return offset;
}

void FrequencyManager::performFrequencyScan(void (*statusCallback)(const char *, const char *))
{
    Serial.println("[FREQ] Starting frequency scan...");
    Serial.println("[FREQ] [NOTE] Wi-Fi/MQTT connections may temporarily drop and reconnect. This is expected.");

    if (statusCallback)
    {
        statusCallback("Frequency Scanning", "Performing frequency scan");
    }

    float bestFreq = s_baseFrequency;
    int bestRSSI = -120; // Start with very low RSSI

    // Scan range: ±30 kHz in 5 kHz steps (±0.03 MHz in 0.005 MHz steps)
    float scanStart = s_baseFrequency - 0.03;
    float scanEnd = s_baseFrequency + 0.03;
    float scanStep = 0.005;

    Serial.printf("[FREQ] Scanning from %.6f to %.6f MHz (step: %.6f MHz)\n", scanStart, scanEnd, scanStep);

    for (float freq = scanStart; freq <= scanEnd; freq += scanStep)
    {
        feedWatchdog();

        // Reinitialize radio with this frequency (via injected callback)
        s_radioInitCallback(freq);
        delay(50); // Allow time for frequency to settle

        // Try to get meter data (via injected callback)
        struct tmeter_data test_data = s_meterReadCallback();

        Serial.printf("[FREQ] Freq %.6f MHz: RSSI=%d dBm, reads=%d\n", freq, test_data.rssi_dbm, test_data.reads_counter);

        if (test_data.rssi_dbm > bestRSSI && test_data.reads_counter > 0)
        {
            bestRSSI = test_data.rssi_dbm;
            bestFreq = freq;
            Serial.printf("[FREQ] Better signal at %.6f MHz: RSSI=%d dBm\n", freq, test_data.rssi_dbm);
        }
    }

    // Calculate and save the offset
    float offset = bestFreq - s_baseFrequency;
    Serial.printf("[FREQ] Frequency scan complete. Best frequency: %.6f MHz (offset: %.6f MHz, RSSI: %d dBm)\n",
                  bestFreq, offset, bestRSSI);

    if (bestRSSI > -120)
    {
        // Found a signal - save offset
        saveFrequencyOffset(offset);

        if (statusCallback)
        {
            char msg[128];
            snprintf(msg, sizeof(msg), "Scan complete: offset %.3f kHz, RSSI %d dBm", offset * 1000.0, bestRSSI);
            statusCallback("Idle", msg);
        }

        // Reinitialize with the best frequency
        s_radioInitCallback(bestFreq);
    }
    else
    {
        Serial.println("[FREQ] Frequency scan failed - no valid signal found");

        if (statusCallback)
        {
            statusCallback("Idle", "Frequency scan failed - no signal");
        }

        // Restore original frequency
        s_radioInitCallback(s_baseFrequency + s_storedOffset);
    }
}

void FrequencyManager::performWideInitialScan(void (*statusCallback)(const char *, const char *))
{
    Serial.println("[FREQ] Performing wide initial scan (first boot - no saved offset)...");

    if (statusCallback)
    {
        statusCallback("Initial Frequency Scan", "First boot: scanning for meter frequency");
    }

    float bestFreq = s_baseFrequency;
    int bestRSSI = -120;

    // Wide scan: ±100 kHz in 10 kHz steps for faster initial discovery
    float scanStart = s_baseFrequency - 0.10;
    float scanEnd = s_baseFrequency + 0.10;
    float scanStep = 0.010;

    Serial.printf("[FREQ] Wide scan from %.6f to %.6f MHz (step: %.6f MHz)\n", scanStart, scanEnd, scanStep);
    Serial.println("[FREQ] This may take 1-2 minutes on first boot...");

    for (float freq = scanStart; freq <= scanEnd; freq += scanStep)
    {
        feedWatchdog();

        // Check if radio initialization succeeds
        if (!s_radioInitCallback(freq))
        {
            Serial.println("[FREQ] Radio not responding - skipping wide initial scan");
            Serial.println("[FREQ] Check: 1) Wiring connections 2) 3.3V power supply 3) SPI pins");

            if (statusCallback)
            {
                statusCallback("Error", "[ERROR] Radio not responding - cannot scan");
            }

            return; // Exit scan
        }

        delay(100); // Longer delay for frequency to settle during wide scan

        struct tmeter_data test_data = s_meterReadCallback();

        if (test_data.rssi_dbm > bestRSSI && test_data.reads_counter > 0)
        {
            bestRSSI = test_data.rssi_dbm;
            bestFreq = freq;
            Serial.printf("[FREQ] Found signal at %.6f MHz: RSSI=%d dBm\n", freq, test_data.rssi_dbm);
        }
    }

    if (bestRSSI > -120)
    {
        // Found a signal - perform fine scan around it
        Serial.printf("[FREQ] Performing fine scan around %.6f MHz...\n", bestFreq);
        float fineStart = bestFreq - 0.015;
        float fineEnd = bestFreq + 0.015;
        float fineStep = 0.003;
        int fineBestRSSI = bestRSSI;
        float fineBestFreq = bestFreq;

        for (float freq = fineStart; freq <= fineEnd; freq += fineStep)
        {
            feedWatchdog();

            if (!s_radioInitCallback(freq))
            {
                Serial.println("[FREQ] Radio not responding during fine scan - aborting");
                break;
            }

            delay(50);

            struct tmeter_data test_data = s_meterReadCallback();

            if (test_data.rssi_dbm > fineBestRSSI && test_data.reads_counter > 0)
            {
                fineBestRSSI = test_data.rssi_dbm;
                fineBestFreq = freq;
                Serial.printf("[FREQ] Refined signal at %.6f MHz: RSSI=%d dBm\n", freq, test_data.rssi_dbm);
            }
        }

        bestFreq = fineBestFreq;
        bestRSSI = fineBestRSSI;

        float offset = bestFreq - s_baseFrequency;
        Serial.printf("[FREQ] Initial scan complete! Best frequency: %.6f MHz (offset: %.6f MHz, RSSI: %d dBm)\n",
                      bestFreq, offset, bestRSSI);

        saveFrequencyOffset(offset);

        if (statusCallback)
        {
            char msg[128];
            snprintf(msg, sizeof(msg), "Initial scan complete: offset %.3f kHz", offset * 1000.0);
            statusCallback("Idle", msg);
        }

        s_radioInitCallback(bestFreq);
    }
    else
    {
        Serial.println("[FREQ] Wide scan failed - no meter signal found!");
        Serial.println("[FREQ] Please check:");
        Serial.println("[FREQ]  1. Meter is within range (< 50m typically)");
        Serial.println("[FREQ]  2. Antenna is connected to CC1101");
        Serial.println("[FREQ]  3. Meter serial/year are correct");
        Serial.println("[FREQ]  4. Current time is within meter's wake hours");

        if (statusCallback)
        {
            statusCallback("Idle", "Initial scan failed - check setup");
        }

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

    Serial.printf("[FREQ] FREQEST: %d (%.4f kHz error), cumulative: %.4f kHz over %d reads\n",
                  freqest, freqErrorMHz * 1000, s_cumulativeFreqError * 1000, s_successfulReadsCount);

    // Only adapt after N successful reads to avoid over-correcting on noise
    if (s_successfulReadsCount >= s_adaptiveThreshold)
    {
        float avgError = s_cumulativeFreqError / s_adaptiveThreshold;

        // Only adjust if average error is significant (> 2 kHz)
        if (abs(avgError * 1000) > ADAPT_MIN_ERROR_KHZ)
        {
            Serial.printf("[FREQ] Adaptive adjustment: average error %.4f kHz over %d reads\n",
                          avgError * 1000, s_adaptiveThreshold);

            // Adjust the stored offset (apply 50% of the measured error to avoid over-correction)
            float adjustment = avgError * ADAPT_CORRECTION_FACTOR;
            s_storedOffset += adjustment;

            Serial.printf("[FREQ] Adjusting frequency offset by %.6f MHz (new offset: %.6f MHz)\n",
                          adjustment, s_storedOffset);

            saveFrequencyOffset(s_storedOffset);

            // Reinitialize radio with adjusted frequency
            s_radioInitCallback(s_baseFrequency + s_storedOffset);
        }
        else
        {
            Serial.printf("[FREQ] Frequency stable (avg error %.4f kHz < %.1f kHz threshold)\n",
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
