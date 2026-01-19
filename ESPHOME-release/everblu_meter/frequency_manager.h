/**
 * @file frequency_manager.h
 * @brief Frequency offset management and calibration for CC1101 radio
 *
 * Provides comprehensive frequency management features for accurate meter communication:
 * - Persistent storage of frequency offsets
 * - Automatic wide-band frequency scanning (first boot)
 * - Fine frequency scanning for optimization
 * - Adaptive frequency tracking using FREQEST
 *
 * This module is designed to be reusable across different projects (Arduino, ESPHome, etc.)
 * and is independent of MQTT or WiFi dependencies.
 */

#ifndef FREQUENCY_MANAGER_H
#define FREQUENCY_MANAGER_H

#include <Arduino.h>

// Only include storage abstraction for standalone builds, not ESPHome
#if !defined(USE_ESPHOME)
#include "storage_abstraction.h"
#endif

// Meter data structure - define only if not already defined by cc1101.h
// ESPHome or other projects should define this structure with equivalent fields
#ifndef __CC1101_H__
struct tmeter_data
{
    int volume;             // Current consumption reading in liters (water) or cubic meters (gas)
    int reads_counter;      // Number of times meter has been read (0 = no data received)
    int battery_left;       // Estimated battery life remaining in months
    int time_start;         // Reading window start time (24-hour format)
    int time_end;           // Reading window end time (24-hour format)
    int rssi;               // Radio Signal Strength Indicator (raw value)
    int rssi_dbm;           // RSSI converted to dBm
    int lqi;                // Link Quality Indicator (0-255)
    int8_t freqest;         // Frequency offset estimate for adaptive tracking
    uint32_t history[13];   // Monthly historical readings (13 months)
    bool history_available; // True if historical data was extracted
};
#endif

/**
 * @typedef RadioInitCallback
 * @brief Function pointer for initializing radio at specific frequency
 * @param freq Frequency in MHz
 * @return true if initialization succeeded, false otherwise
 */
typedef bool (*RadioInitCallback)(float freq);

/**
 * @typedef MeterReadCallback
 * @brief Function pointer for reading meter data
 * @return tmeter_data structure with meter readings
 */
typedef tmeter_data (*MeterReadCallback)();

/**
 * @typedef StatusCallback
 * @brief Function pointer for status updates (optional, for MQTT/logging)
 * @param state Current state (e.g., "Scanning", "Idle")
 * @param message Detailed status message
 */
typedef void (*StatusCallback)(const char *state, const char *message);

/**
 * @class FrequencyManager
 * @brief Manages frequency calibration and optimization for CC1101-based meter communication
 *
 * This class is designed for maximum reusability across projects (Arduino, ESPHome, etc.)
 * by using dependency injection for radio operations. It has NO hard dependencies on
 * specific implementations - you inject your own radio init and meter read functions.
 *
 * **ESPHome Integration Example:**
 * ```cpp
 * // In your ESPHome component:
 * FrequencyManager::setRadioInitCallback([](float freq) {
 *   return this->cc1101_init(freq);
 * });
 * FrequencyManager::setMeterReadCallback([]() {
 *   return this->get_meter_data();
 * });
 * FrequencyManager::begin(433.82);
 * ```
 *
 * Handles all aspects of frequency management including:
 * - Loading/saving frequency offsets to persistent storage
 * - Performing frequency scans (wide and narrow range)
 * - Adaptive tracking based on FREQEST readings
 * - MQTT status reporting (optional, via callbacks)
 */
class FrequencyManager
{
public:
    /**
     * @brief Set radio initialization callback (REQUIRED)
     *
     * Inject your radio initialization function. This will be called whenever
     * the frequency needs to be changed (during scans or adjustments).
     *
     * @param callback Function that initializes radio at given frequency
     *
     * Example: `FrequencyManager::setRadioInitCallback(cc1101_init);`
     */
    static void setRadioInitCallback(RadioInitCallback callback);

    /**
     * @brief Set meter read callback (REQUIRED)
     *
     * Inject your meter reading function. This will be called during frequency
     * scans to test signal quality at different frequencies.
     *
     * @param callback Function that reads meter data
     *
     * Example: `FrequencyManager::setMeterReadCallback(get_meter_data);`
     */
    static void setMeterReadCallback(MeterReadCallback callback);

    /**
     * @brief Initialize frequency manager
     *
     * Sets up storage and loads any previously saved frequency offset.
     * **IMPORTANT:** Call setRadioInitCallback() and setMeterReadCallback() BEFORE this.
     * @param baseFrequency Base meter frequency in MHz (e.g., 433.82)
     * @return Loaded frequency offset in MHz (0.0 if none saved)
     */
    static float begin(float baseFrequency);

    /**
     * @brief Get the stored frequency offset
     *
     * @return Current frequency offset in MHz
     */
    static float getOffset();

    /**
     * @brief Set the frequency offset (and save to storage)
     *
     * @param offset Frequency offset in MHz
     */
    static void setOffset(float offset);

    /**
     * @brief Save frequency offset to persistent storage
     *
     * Stores the offset value with validation magic number.
     *
     * @param offset Frequency offset in MHz to save
     */
    static void saveFrequencyOffset(float offset);

    /**
     * @brief Load frequency offset from persistent storage
     *
     * Retrieves previously saved offset with validation.
     *
     * @return Frequency offset in MHz (0.0 if not found or invalid)
     */
    static float loadFrequencyOffset();

    /**
     * @brief Perform narrow-range frequency scan
     *
     * Scans ±30 kHz around current frequency in 5 kHz steps to find optimal signal.
     * Updates stored offset if better frequency is found.
     * Recommended for periodic recalibration.
     *
     * @param statusCallback Optional callback for status updates (can be nullptr)
     */
    static void performFrequencyScan(void (*statusCallback)(const char *state, const char *message) = nullptr);

    /**
     * @brief Perform wide-range initial frequency scan
     *
     * Comprehensive scan over ±100 kHz in 10 kHz steps, followed by fine scan.
     * Used on first boot when no offset is saved. Takes 1-2 minutes.
     *
     * @param statusCallback Optional callback for status updates (can be nullptr)
     */
    static void performWideInitialScan(void (*statusCallback)(const char *state, const char *message) = nullptr);

    /**
     * @brief Adaptive frequency tracking using FREQEST
     *
     * Accumulates frequency error estimates from CC1101 FREQEST register over
     * multiple successful reads. When threshold is reached, applies correction
     * and reinitializes radio.
     *
     * Call this after each successful meter read with the freqest value.
     *
     * @param freqest Frequency offset estimate from CC1101 (-128 to +127)
     */
    static void adaptiveFrequencyTracking(int8_t freqest);

    /**
     * @brief Reset adaptive tracking accumulators
     *
     * Clears the accumulated frequency error and read counter.
     * Call this if you want to restart the adaptive tracking cycle.
     */
    static void resetAdaptiveTracking();

    /**
     * @brief Check if auto-scan should run on first boot
     *
     * @return true if auto-scan is enabled and no offset is saved
     */
    static bool shouldPerformAutoScan();

    /**
     * @brief Get the base frequency
     *
     * @return Base frequency in MHz
     */
    static float getBaseFrequency();

    /**
     * @brief Get the current tuned frequency (base + offset)
     *
     * @return Tuned frequency in MHz
     */
    static float getTunedFrequency();

    /**
     * @brief Configuration: Enable/disable auto-scan on first boot
     */
    static void setAutoScanEnabled(bool enabled);

    /**
     * @brief Configuration: Set adaptive tracking threshold
     *
     * Number of successful reads before applying frequency correction.
     *
     * @param threshold Number of reads (default: 10)
     */
    static void setAdaptiveThreshold(int threshold);

private:
    // Configuration
    static float s_baseFrequency;   // Base meter frequency (e.g., 433.82 MHz)
    static float s_storedOffset;    // Current frequency offset in MHz
    static bool s_autoScanEnabled;  // Enable auto-scan on first boot
    static int s_adaptiveThreshold; // Reads before adapting (default: 10)

    // Adaptive tracking state
    static int s_successfulReadsCount;  // Counter for adaptive tracking
    static float s_cumulativeFreqError; // Accumulated frequency error in MHz

    // Injected callbacks (dependency injection for reusability)
    static RadioInitCallback s_radioInitCallback; // Radio initialization function
    static MeterReadCallback s_meterReadCallback; // Meter reading function

    // Constants
    static constexpr float FREQEST_TO_MHZ = 0.001587;     // ~1.59 kHz per LSB (26 MHz crystal)
    static constexpr float MIN_OFFSET = -0.1;             // Min offset: -100 kHz
    static constexpr float MAX_OFFSET = 0.1;              // Max offset: +100 kHz
    static constexpr float ADAPT_MIN_ERROR_KHZ = 2.0;     // Min error to trigger adaptation (kHz)
    static constexpr float ADAPT_CORRECTION_FACTOR = 0.5; // Apply 50% correction to avoid oscillation

    // Storage key for frequency offset
    static constexpr const char *STORAGE_KEY = "freq_offset";
    static constexpr uint16_t STORAGE_MAGIC = 0xABCD;

    // Helper functions
    static void feedWatchdog();
    static bool validateCallbacks(); // Validate that required callbacks are set

    // Private constructor - static-only class
    FrequencyManager() = delete;
};

#endif // FREQUENCY_MANAGER_H
