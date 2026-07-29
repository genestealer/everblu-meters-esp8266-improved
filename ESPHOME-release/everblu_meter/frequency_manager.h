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

// struct tmeter_data has exactly one definition, in cc1101.h. Never duplicate
// it here: MeterReadCallback returns a tmeter_data by value across translation
// units, so a divergent local copy silently undersizes the caller's return slot
// and corrupts the stack during a scan.
#if __has_include("../core/cc1101.h")
#include "cc1101.h"
#elif __has_include("cc1101.h")
#include "cc1101.h"
#else
#error "Missing cc1101.h (defines struct tmeter_data)"
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
     * @brief Perform a Deep frequency scan (blocking)
     *
     * Scans +-150 kHz (default) around the base frequency in fine 2.5 kHz steps for a
     * thorough sweep. Maps the response window then zooms to the exact carrier centre.
     * Also used on first boot when no offset is saved.
     *
     * Convenience wrapper around beginDeepFrequencyScan() + loopScan(); it does not
     * return until the scan finishes, so the caller's loop is stalled for the whole
     * sweep. Hosts with a cooperative main loop (ESPHome) should drive the scan with
     * beginDeepFrequencyScan()/loopScan() instead so the Stop button stays responsive.
     *
     * @param scanRangeMHz Half-width of scan in MHz. Default 0.150 (±150 kHz full sweep).
     *                    Pass 0.020 for a narrow ±20 kHz re-tune after a drift failure.
     * @param scanStepMHz Step size in MHz. Default 0.0025 (2.5 kHz).
     *                    Pass 0.001 for 1 kHz steps in a narrow scan.
     * @param statusCallback Optional callback for status updates (can be nullptr)
     */
    static void performDeepFrequencyScan(
        float scanRangeMHz = 0.150f,
        float scanStepMHz = 0.0025f,
        void (*statusCallback)(const char *state, const char *message) = nullptr);

    /**
     * @brief Start a Deep frequency scan and return immediately
     *
     * The scan then advances by one frequency step per loopScan() call, which keeps
     * the host loop responsive so an incoming Stop command can still be delivered and
     * requestScanCancel() observed mid-scan.
     *
     * @param scanRangeMHz Half-width of scan in MHz (see performDeepFrequencyScan)
     * @param scanStepMHz Step size in MHz (see performDeepFrequencyScan)
     * @param statusCallback Optional callback for status updates (can be nullptr)
     */
    static void beginDeepFrequencyScan(
        float scanRangeMHz = 0.150f,
        float scanStepMHz = 0.0025f,
        void (*statusCallback)(const char *state, const char *message) = nullptr);

    /**
     * @brief Check whether a scan started by beginDeepFrequencyScan() is still running
     * @return true while the scan state machine has work left to do
     */
    static bool isScanInProgress();

    /**
     * @brief Advance the scan by one step
     *
     * Performs at most one radio transaction per call. No-op when no scan is running.
     */
    static void loopScan();

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
     * @brief Request cancellation of an in-progress deep frequency scan.
     *
     * The scan checks this between frequency steps and bails out at the next
     * step boundary (it cannot interrupt the blocking read within a single
     * step). Has no effect once the scan has already finished.
     */
    static void requestScanCancel();

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
    /**
     * @brief Phases of the non-blocking deep scan state machine
     */
    enum class ScanPhase : uint8_t
    {
        Idle,            // No scan running
        WindowMap,       // Coarse sweep mapping the response window
        Zoom,            // Fine sweep across the discovered window
        VerifyCandidate, // Post-lock verification of the scan candidate
        VerifyStored     // Verification of the previously stored offset
    };

    /**
     * @brief Persisted state of the non-blocking deep scan
     *
     * Holds everything the old blocking loop kept in locals, so a scan can be
     * suspended between frequency steps and resumed from the host loop.
     */
    struct ScanState
    {
        ScanPhase phase;               // Current phase (Idle when no scan is running)
        StatusCallback statusCallback; // Optional status reporting hook
        float freq;                    // Next frequency to test
        float scanEnd;                 // Last frequency of the window-map sweep
        float step;                    // Window-map step size in MHz
        float firstHitFreq;            // Start of the discovered response window (-1 = none yet)
        float lastHitFreq;             // End of the discovered response window
        int bestRSSI;                  // Best RSSI seen so far
        int consecutiveMisses;         // Misses since the last hit (window-end detection)
        float previousOffset;          // Known-good offset snapshot (quality guard, issue #104)
        float zoomEnd;                 // Last frequency of the zoom sweep
        float zoomStep;                // Zoom step size in MHz
        float bestFreq;                // Best candidate frequency found
        int candQuality;               // |FREQEST| of the candidate (smaller = better centred)
        bool quietPrevious;            // Saved g_echo_debug_quiet (RAII guard cannot span loops)
    };

    // Configuration
    static float s_baseFrequency;   // Base meter frequency (e.g., 433.82 MHz)
    static float s_storedOffset;    // Current frequency offset in MHz
    static bool s_autoScanEnabled;      // Enable auto-scan on first boot
    static bool s_hasStoredCalibration;  // True when a non-default offset was loaded from storage
    static volatile bool s_scanCancelRequested; // Set by requestScanCancel(), checked between deep-scan steps
    static int s_adaptiveThreshold; // Reads before adapting (default: 10)

    // Adaptive tracking state
    static int s_successfulReadsCount;  // Counter for adaptive tracking
    static float s_cumulativeFreqError; // Accumulated frequency error in MHz

    // Non-blocking deep scan state
    static ScanState s_scan;

    // Injected callbacks (dependency injection for reusability)
    static RadioInitCallback s_radioInitCallback; // Radio initialization function
    static MeterReadCallback s_meterReadCallback; // Meter reading function

    // Constants
    static constexpr float FREQEST_TO_MHZ = 0.001587;     // ~1.59 kHz per LSB (26 MHz crystal)
    static constexpr float MIN_OFFSET = -0.1;             // Min offset: -100 kHz
    static constexpr float MAX_OFFSET = 0.1;              // Max offset: +100 kHz
    static constexpr float ADAPT_MIN_ERROR_KHZ = 2.0;     // Min error to trigger adaptation (kHz)
    static constexpr float ADAPT_CORRECTION_FACTOR = 0.5; // Apply 50% correction to avoid oscillation
    static constexpr int MISS_TOLERANCE = 5;              // Consecutive misses that close the response window
    // CC1101 minimum frequency step = Fxosc / 2^16 = 26 MHz / 65536 ~ 397 Hz.
    // Steps finer than this round to the same register value, silently retesting
    // the same physical frequency. Zoom steps are clamped to at least 1 register step.
    static constexpr float CC1101_MIN_STEP_MHZ = 26.0f / 65536.0f / 1000.0f;

    // Storage key for frequency offset
    static constexpr const char *STORAGE_KEY = "freq_offset";
    static constexpr uint16_t STORAGE_MAGIC = 0xABCD;

    // Helper functions
    static void feedWatchdog();
    static bool validateCallbacks(); // Validate that required callbacks are set

    // Deep scan state machine helpers
    static void finishScan(const char *state, const char *message);
    static void beginZoomOrFail();
    static void stepWindowMap();
    static void stepZoom();
    static void stepVerifyCandidate();
    static void stepVerifyStored();
    static void stepFinalise(bool acceptCandidate);

    // Private constructor - static-only class
    FrequencyManager() = delete;
};

#endif // FREQUENCY_MANAGER_H
