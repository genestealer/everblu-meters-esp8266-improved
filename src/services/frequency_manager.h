/**
 * @file frequency_manager.h
 * @brief Frequency offset management and calibration for CC1101 radio
 *
 * Provides comprehensive frequency management features for accurate meter communication:
 * - Persistent storage of frequency offsets, one calibration per meter
 * - Staged frequency scanning (coarse acquisition, bracketing, fine sweep, verification)
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
#include "../core/cc1101.h"
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
 * @param state Current state (e.g., "Frequency Scanning", "Idle")
 * @param message Detailed status message
 */
typedef void (*StatusCallback)(const char *state, const char *message);

/**
 * @class FrequencyManager
 * @brief Manages frequency calibration and scanning for CC1101-based meter communication
 *
 * Uses dependency injection for radio operations, so it has NO hard dependency on a
 * specific radio or transport: callers inject their own radio init and meter read
 * functions and the same engine serves both the MQTT and ESPHome builds.
 *
 * Calibration is PER METER, not per radio. Each meter owns a Calibration holding its
 * own base frequency, offset, storage key and adaptive-tracking accumulator. The radio
 * itself is shared, so exactly one Calibration is active at a time (see
 * activateCalibration) and a running scan keeps ownership until it finishes.
 */
class FrequencyManager
{
public:
    /**
     * @brief One meter's persistent frequency calibration and tracking state
     *
     * Held by the caller (typically a MeterReader) rather than by this class, so several
     * meters sharing one CC1101 keep independent calibrations. The destructor releases
     * the active pointer, so destroying the owner cannot leave a dangling calibration.
     */
    struct Calibration
    {
        float baseFrequency = 0.0f;   // Configured meter frequency in MHz (e.g. 433.82)
        float offset = 0.0f;          // Calibration offset in MHz, added to baseFrequency
        bool autoScan = true;         // Allow a first-boot scan when nothing is stored
        bool hasStored = false;       // True once a value has been persisted for this meter
        int adaptiveThreshold = 10;   // Successful reads before adaptive tracking adjusts
        int successfulReads = 0;      // Reads accumulated toward the threshold
        float cumulativeError = 0.0f; // Accumulated FREQEST error in MHz
        char storageKey[24] = "freq_offset"; // Per-meter persistence key
        ~Calibration();
    };

    /**
     * @brief Result of the most recently finished scan
     *
     * Lets a caller escalate only when a sweep genuinely came back empty, rather than
     * after a user cancel or a radio fault, where repeating would change nothing.
     */
    enum class ScanOutcome : uint8_t
    {
        None,     // No scan has finished yet, or one is still running
        Found,    // A verified calibration was accepted
        NotFound, // The range was swept and nothing responded (or verification failed)
        Aborted   // Cancelled by the user, or the radio stopped responding
    };

    /** @brief Set the radio initialisation callback (REQUIRED before begin()). */
    static void setRadioInitCallback(RadioInitCallback callback);

    /** @brief Set the meter read callback (REQUIRED before begin()). */
    static void setMeterReadCallback(MeterReadCallback callback);

    /**
     * @brief Initialise the built-in single-meter calibration
     *
     * Convenience entry point for the standalone MQTT build, which has one meter and
     * one storage key. Multi-meter hosts should own a Calibration each and call
     * initialiseCalibration() instead.
     *
     * @param baseFrequency Base meter frequency in MHz (e.g., 433.82)
     * @return Loaded frequency offset in MHz (0.0 if none saved)
     */
    static float begin(float baseFrequency);

    /**
     * @brief Load a caller-owned calibration from storage and make it active
     * @param storageKey Persistence key; must be unique per meter
     */
    static void initialiseCalibration(Calibration &calibration, float baseFrequency, const char *storageKey);

    /**
     * @brief Point the manager at a calibration before touching the radio
     * @return false when a scan owned by a DIFFERENT calibration is still running
     */
    static bool activateCalibration(Calibration &calibration);

    /** @brief Detach a calibration, ending any scan it owns. Called by ~Calibration(). */
    static void releaseCalibration(Calibration &calibration);

    /** @brief Get the active calibration's offset in MHz. */
    static float getOffset();

    /** @brief Set the active calibration's offset in MHz without persisting it. */
    static void setOffset(float offset);

    /** @brief Persist an offset for the active calibration (range-checked). */
    static void saveFrequencyOffset(float offset);

    /** @brief Read the active calibration's stored offset straight from storage. */
    static float loadFrequencyOffset();

    /** @brief Get the active calibration's base frequency in MHz. */
    static float getBaseFrequency();

    /** @brief Get the active calibration's tuned frequency (base + offset) in MHz. */
    static float getTunedFrequency();

    /** @brief Configuration: allow a first-boot scan for the active calibration. */
    static void setAutoScanEnabled(bool enabled);

    /** @brief Configuration: successful reads before adaptive tracking adjusts. */
    static void setAdaptiveThreshold(int threshold);

    /** @brief True when auto-scan is enabled and this meter has no stored calibration. */
    static bool shouldPerformAutoScan();

    /**
     * @brief Adaptive frequency tracking using FREQEST
     *
     * Accumulates frequency error over successful reads and applies half of the average
     * once the threshold is reached, which avoids oscillating around the carrier.
     * Call this after each successful meter read.
     *
     * @param freqest Frequency offset estimate from CC1101 (-128 to +127)
     */
    static void adaptiveFrequencyTracking(int8_t freqest);

    /** @brief Clear the adaptive tracking accumulator and read counter. */
    static void resetAdaptiveTracking();

    /**
     * @brief Run a staged frequency scan to completion (blocking)
     *
     * Convenience wrapper around beginDeepFrequencyScan() + loopScan(); it does not
     * return until the scan finishes, so the caller's loop is stalled for the whole
     * sweep. Hosts with a cooperative main loop should drive the scan with
     * beginDeepFrequencyScan()/loopScan() instead so Stop stays responsive (#133).
     *
     * @param scanRangeMHz Half-width of the sweep in MHz. Default 0.150 (±150 kHz).
     * @param scanStepMHz Coarse acquisition step in MHz. Default 0.010 (~10 kHz).
     * @param statusCallback Optional per-phase status hook (can be nullptr)
     */
    static void performDeepFrequencyScan(float scanRangeMHz = 0.150f, float scanStepMHz = 0.010f,
                                         StatusCallback statusCallback = nullptr);

    /**
     * @brief Start a staged frequency scan and return immediately
     *
     * The scan advances by one radio transaction per loopScan() call, which keeps the
     * host loop responsive so an incoming Stop can still be delivered mid-scan (#133).
     *
     * Stages: coarse acquisition across the range; one finer acquisition pass if that
     * came back empty; bracketing of both response edges; a full fine sweep of the
     * bracket; then verification before anything is saved.
     */
    static void beginDeepFrequencyScan(float scanRangeMHz = 0.150f, float scanStepMHz = 0.010f,
                                       StatusCallback statusCallback = nullptr);

    /**
     * @brief Start a scan centred on the saved tuning rather than the configured base
     *
     * Ordinary drift is recovered by a short local sweep. If that finds nothing the scan
     * widens itself to the full range, so drift larger than the local window does not
     * leave the meter unreachable until someone presses Deep Scan by hand.
     */
    static void beginRecoveryScan(StatusCallback statusCallback = nullptr);

    /** @brief True while the scan state machine still has work to do. */
    static bool isScanInProgress();

    /** @brief Outcome of the last finished scan. */
    static ScanOutcome lastScanOutcome();

    /**
     * @brief Advance the scan by one step
     *
     * Performs at most one radio transaction per call. No-op when no scan is running.
     */
    static void loopScan();

    /**
     * @brief Request cancellation of an in-progress scan
     *
     * Checked between radio transactions; a transfer already in flight cannot be
     * interrupted. Cancelling restores the previous tuning.
     */
    static void requestScanCancel();

private:
    /**
     * @brief Stages of the non-blocking scan state machine
     *
     * Acquire finds any response; Lower/Upper map the edges of the response window;
     * Zoom samples the whole window finely; the Verify stages confirm a candidate
     * against repeat reads before Finalise persists it.
     */
    enum class ScanPhase : uint8_t
    {
        Idle,
        Acquire,
        Lower,
        Upper,
        Zoom,
        VerifyCandidate,
        VerifyStored,
        Finalise
    };

    /**
     * @brief Decode reliability at one frequency
     *
     * Successes are ranked before error because a frequency that answers consistently
     * is worth more than one that answered once with a slightly better FREQEST.
     */
    struct Quality
    {
        int attempts = 0;  // Reads performed
        int successes = 0; // Reads that decoded
        int error = 0;     // Sum of |FREQEST| over the successful reads
    };

    /**
     * @brief Persisted state of the non-blocking scan
     *
     * Frequencies are held as integer CC1101 frequency words, not floats, so repeated
     * stepping cannot accumulate rounding error and every step lands on a distinct
     * tunable frequency.
     */
    struct ScanState
    {
        ScanPhase phase = ScanPhase::Idle;
        StatusCallback statusCallback = nullptr;
        int32_t start = 0;          // First frequency word of the sweep
        int32_t end = 0;            // Last frequency word of the sweep
        int32_t current = 0;        // Frequency word being tested
        int32_t step = 0;           // Acquisition stride in frequency words
        int32_t seed = 0;           // Word where acquisition first got a response
        int32_t firstHit = 0;       // Lowest word that answered
        int32_t lastHit = 0;        // Highest word that answered
        int32_t zoomStart = 0;      // First word of the fine sweep
        int32_t zoomEnd = 0;        // Last word of the fine sweep
        int32_t best = 0;           // Best candidate word found by the fine sweep
        int misses = 0;             // Consecutive misses while bracketing an edge
        bool expandOnMiss = false;  // Recovery scan: widen to full range if local is empty
        bool finerFallback = false; // One finer acquisition pass is still available
        bool quietPrevious = false; // Saved g_echo_debug_quiet (RAII cannot span loops)
        bool acceptCandidate = false;
        float previousOffset = 0.0f; // Known-good offset snapshot (quality guard, issue #104)
        Quality sample;              // Repeat reads at the frequency under test
        Quality bestQuality;         // Quality of the best candidate so far
        Quality candidateQuality;    // Verification result for the candidate
        Quality storedQuality;       // Verification result for the existing calibration
    };

    // Fallback calibration used by begin(); multi-meter hosts supply their own.
    static Calibration s_defaultCalibration;
    static Calibration *s_calibration; // Never null; points at s_defaultCalibration when idle
    static ScanState s_scan;
    static ScanOutcome s_lastScanOutcome;
    static bool s_scanCancelRequested; // Set by requestScanCancel(), checked between steps

    // Injected callbacks (dependency injection for reusability)
    static RadioInitCallback s_radioInitCallback;
    static MeterReadCallback s_meterReadCallback;

    // CC1101 frequency word resolution = Fxosc / 2^16 = 26 MHz / 65536 ~ 397 Hz.
    // Frequencies here are in MHz, so this must NOT also be divided by 1000.
    static constexpr float CC1101_MIN_STEP_MHZ = 26.0f / 65536.0f;
    static constexpr float FREQEST_TO_MHZ = 0.001587f; // ~1.59 kHz per LSB (26 MHz crystal)
    // Scan bounds match the persistence range, so an offset found at the edge of a
    // sweep is still accepted by the loader after a reboot.
    static constexpr float MIN_OFFSET = -0.150f;
    static constexpr float MAX_OFFSET = 0.150f;
    static constexpr uint16_t STORAGE_MAGIC = 0xABCD;
    // A single missed reply is not an edge: the meter does not answer every attempt,
    // so an edge is only closed after this many consecutive misses.
    static constexpr int MISS_TOLERANCE = 5;
    static constexpr int MAP_STEP = 6;  // Bracketing/fallback stride, ~2.380 kHz
    static constexpr int ZOOM_STEP = 2; // Fine sweep stride, ~793 Hz

    static void feedWatchdog();
    static bool validateCallbacks(); // Validate that required callbacks are set
    static void reportPhase(const char *message);

    // Scan state machine helpers
    static void startSweep(float centre, float range, float step);
    static bool readAt(float frequency, tmeter_data &data);
    static void record(Quality &quality, const tmeter_data &data);
    static bool better(const Quality &candidate, const Quality &previous);
    static void finishScan(ScanOutcome outcome, const char *message);
    static void stepAcquire();
    static void stepBracket();
    static void stepZoom();
    static void stepVerify();

    // Private constructor - static-only class
    FrequencyManager() = delete;
};

#endif // FREQUENCY_MANAGER_H