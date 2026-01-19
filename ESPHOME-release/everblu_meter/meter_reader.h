/**
 * @file meter_reader.h
 * @brief Orchestrator for meter reading operations
 *
 * Coordinates meter reading, scheduling, and data publishing using
 * abstract interfaces. This is the main entry point for meter operations
 * and is platform-agnostic through dependency injection.
 *
 * Usage:
 * ```cpp
 * MeterReader reader(configProvider, timeProvider, dataPublisher);
 * reader.begin();
 * // In loop:
 * reader.loop();
 * // Manual trigger:
 * reader.triggerReading(false); // false = not scheduled
 * ```
 */

#ifndef METER_READER_H
#define METER_READER_H

#include "config_provider.h"
#include "time_provider.h"
#include "data_publisher.h"
#include "cc1101.h"
#include "frequency_manager.h"

/**
 * @class MeterReader
 * @brief Orchestrates meter reading operations with scheduling
 *
 * This class is the core coordinator for all meter reading operations.
 * It uses dependency injection to work with different platforms
 * (standalone MQTT, ESPHome, etc.) without modification.
 */
class MeterReader
{
public:
    /**
     * @brief Constructor
     * @param config Configuration provider
     * @param timeProvider Time synchronization provider
     * @param publisher Data publisher
     */
    MeterReader(IConfigProvider *config, ITimeProvider *timeProvider, IDataPublisher *publisher);

    /**
     * @brief Initialize meter reader and subsystems
     *
     * Initializes:
     * - CC1101 radio
     * - FrequencyManager
     * - Scheduling engine
     */
    void begin();

    /**
     * @brief Main loop processing
     *
     * Should be called regularly from main loop.
     * Handles:
     * - Schedule checking
     * - Retry management
     * - Statistics updates
     */
    void loop();

    /**
     * @brief Trigger a manual meter reading
     * @param isScheduled true if triggered by schedule, false if manual
     */
    void triggerReading(bool isScheduled);

    /**
     * @brief Perform frequency scan
     * @param wideRange true for wide scan, false for narrow
     */
    void performFrequencyScan(bool wideRange);

    /**
     * @brief Reset frequency offset to 0
     */
    void resetFrequencyOffset();

    /**
     * @brief Get current read statistics
     * @param totalAttempts Output: total read attempts
     * @param successfulReads Output: successful reads
     * @param failedReads Output: failed reads
     */
    void getStatistics(unsigned long &totalAttempts, unsigned long &successfulReads,
                       unsigned long &failedReads) const;

    /**
     * @brief Check if a reading is currently in progress
     * @return true if reading active
     */
    bool isReadingInProgress() const { return m_readingInProgress; }

    /**
     * @brief Get last error message
     * @return Error message string
     */
    const char *getLastError() const { return m_lastErrorMessage; }

    /**
     * @brief Set Home Assistant connection state (ESPHome builds)
     * @param connected true when HA is connected via ESPHome API
     */
    void setHAConnected(bool connected);

private:
    /**
     * @brief Perform actual meter reading operation
     *
     * Handles:
     * - Radio communication
     * - Data validation
     * - Retry logic
     * - Publishing results
     */
    void performReading();

    /**
     * @brief Check if it's time for a scheduled reading
     * @return true if schedule conditions are met
     */
    bool shouldPerformScheduledRead();

    /**
     * @brief Handle successful reading
     * @param data Meter data
     */
    void handleSuccessfulRead(const tmeter_data &data);

    /**
     * @brief Handle failed reading attempt
     */
    void handleFailedRead();

    /**
     * @brief Reset retry counter and cooldown
     */
    void resetRetryState();

    // Dependencies (injected)
    IConfigProvider *m_config;
    ITimeProvider *m_timeProvider;
    IDataPublisher *m_publisher;

    // State tracking
    bool m_initialized;
    bool m_readingInProgress;
    bool m_isScheduledRead;
    bool m_haConnected;

    // Retry management
    int m_retryCount;
    unsigned long m_lastFailedAttempt;
    unsigned long m_nextRetryTime;

    // Statistics
    unsigned long m_totalReadAttempts;
    unsigned long m_successfulReads;
    unsigned long m_failedReads;

    // Error tracking
    const char *m_lastErrorMessage;

    // Timing
    unsigned long m_lastScheduleCheck;
    unsigned long m_lastStatsPublish;

    // Schedule state cache
    int m_readHourLocal;
    int m_readMinuteLocal;
    bool m_lastReadDayMatch;
    bool m_lastReadTimeMatch;
};

#endif // METER_READER_H
