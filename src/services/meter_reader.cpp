/**
 * @file meter_reader.cpp
 * @brief Implementation of MeterReader orchestrator
 */

#include "meter_reader.h"
#include "schedule_manager.h"
#include "meter_history.h"
#include "../core/utils.h"
#include "../core/wifi_serial.h"
#include <Arduino.h>

// Schedule check interval (milliseconds)
static const unsigned long SCHEDULE_CHECK_INTERVAL_MS = 500;

// Statistics publish interval (milliseconds) - 5 minutes
static const unsigned long STATS_PUBLISH_INTERVAL_MS = 300000;

// Retry delay (milliseconds) - 5 seconds between retry attempts
static const unsigned long RETRY_DELAY_MS = 5000;

// Produce a concise, MQTT-style summary of the latest reading for ESPHome logs
static void logReadableSummary(const tmeter_data &data, const IConfigProvider *config)
{
    const bool isGas = config->isMeterGas();
    int volumeDivisor = config->getGasVolumeDivisor();
    if (volumeDivisor <= 0)
    {
        volumeDivisor = 100; // Sensible fallback for gas meters
    }

    // Use shared utility function to print meter data
    printMeterDataSummary(&data, isGas, volumeDivisor);

    // Print historical data if available
    if (data.history_available && MeterHistory::isHistoryValid(data.history))
    {
        MeterHistory::printToSerial(data.history, static_cast<uint32_t>(data.volume), "[HISTORY]");
    }
}

MeterReader::MeterReader(IConfigProvider *config, ITimeProvider *timeProvider, IDataPublisher *publisher)
    : m_config(config), m_timeProvider(timeProvider), m_publisher(publisher), m_initialized(false), m_readingInProgress(false), m_isScheduledRead(false), m_haConnected(false), m_retryCount(0), m_lastFailedAttempt(0), m_nextRetryTime(0), m_totalReadAttempts(0), m_successfulReads(0), m_failedReads(0), m_lastErrorMessage("None"), m_lastScheduleCheck(0), m_lastStatsPublish(0), m_readHourLocal(10), m_readMinuteLocal(0), m_lastReadDayMatch(false), m_lastReadTimeMatch(false)
{
}

void MeterReader::begin()
{
    Serial.println("[MeterReader] Initializing...");

    // Register FrequencyManager callbacks
    FrequencyManager::setRadioInitCallback(cc1101_init);
    FrequencyManager::setMeterReadCallback(get_meter_data);

    // Initialize FrequencyManager with configured frequency
    float frequency = m_config->getFrequency();
    FrequencyManager::begin(frequency);
    FrequencyManager::setAutoScanEnabled(m_config->isAutoScanEnabled());

    // Initialize CC1101 at tuned frequency so we can publish hardware state
    float effectiveFrequency = FrequencyManager::getTunedFrequency();
    if (effectiveFrequency <= 0.0f)
    {
        effectiveFrequency = frequency + FrequencyManager::getOffset();
    }

    bool radio_ok = cc1101_init(effectiveFrequency);

    // Calculate local reading time from UTC and timezone offset
    int utcHour = m_config->getReadHourUTC();
    int utcMinute = m_config->getReadMinuteUTC();
    int offsetMinutes = m_config->getTimezoneOffsetMinutes();

    int totalUtcMin = utcHour * 60 + utcMinute;
    int localMin = (totalUtcMin + offsetMinutes) % (24 * 60);
    if (localMin < 0)
        localMin += 24 * 60;

    m_readHourLocal = localMin / 60;
    m_readMinuteLocal = localMin % 60;

    Serial.printf("[MeterReader] Scheduled reading time: %02d:%02d UTC (%02d:%02d local)\n",
                  utcHour, utcMinute, m_readHourLocal, m_readMinuteLocal);
    Serial.printf("[MeterReader] Reading schedule: %s\n", m_config->getReadingSchedule());

    m_initialized = true;
    Serial.println("[MeterReader] Initialization complete");

    // Publish initial states to all configured sensors
    // In ESPHome mode, this gets republished when HA connects via the component
    // In standalone mode, this ensures sensors don't show "Unknown"
    if (m_publisher)
    {
        Serial.println("[MeterReader] Publishing initial sensor states...");

        if (radio_ok)
        {
            m_publisher->publishRadioState("Idle");
            m_publisher->publishStatusMessage("Ready");
            m_publisher->publishError("None");
        }
        else
        {
            m_publisher->publishRadioState("unavailable");
            m_publisher->publishStatusMessage("Error");
            m_publisher->publishError("CC1101 radio not responding");
        }

        // Publish initial operational states
        m_publisher->publishActiveReading(false);

        // Publish initial statistics (all zeros)
        m_publisher->publishStatistics(0, 0, 0);

        Serial.println("[MeterReader] Initial states published");
    }
    else
    {
        Serial.println("[MeterReader] WARNING: Publisher not available, cannot publish initial states");
    }
}

void MeterReader::loop()
{
    if (!m_initialized)
        return;

    unsigned long now = millis();

    // Check for pending retry
    if (m_retryCount > 0 && m_nextRetryTime > 0 && now >= m_nextRetryTime)
    {
        Serial.printf("[MeterReader] Retry timer expired, attempting retry %d/%d\n",
                      m_retryCount + 1, m_config->getMaxRetries());
        m_nextRetryTime = 0;
        performReading();
        return;
    }

    // Check schedule periodically
    if (now - m_lastScheduleCheck >= SCHEDULE_CHECK_INTERVAL_MS)
    {
        m_lastScheduleCheck = now;

        if (shouldPerformScheduledRead())
        {
            triggerReading(true);
        }
    }

    // Publish statistics periodically
    if (now - m_lastStatsPublish >= STATS_PUBLISH_INTERVAL_MS)
    {
        m_lastStatsPublish = now;
        if (m_publisher->isReady())
        {
            m_publisher->publishStatistics(m_totalReadAttempts, m_successfulReads, m_failedReads);
        }
    }
}

bool MeterReader::shouldPerformScheduledRead()
{
    // Don't trigger if already reading
    if (m_readingInProgress)
        return false;

#ifdef USE_ESPHOME
    // In ESPHome builds, avoid scheduled reads until HA API is connected
    if (!m_haConnected)
    {
        return false;
    }
#endif

    // Don't trigger if time not synchronized
    if (!m_timeProvider->isTimeSynced())
    {
        return false;
    }

    // Check if in cooldown period after failures
    if (m_lastFailedAttempt > 0)
    {
        unsigned long cooldown = m_config->getRetryCooldownMs();
        if (millis() - m_lastFailedAttempt < cooldown)
        {
            return false;
        }
        // Cooldown expired, reset
        m_lastFailedAttempt = 0;
    }

    // Get current local time
    time_t localTime = m_timeProvider->getLocalTime(m_config->getTimezoneOffsetMinutes());
    struct tm *ptm = gmtime(&localTime);

    // Check if today is a valid reading day
    bool isDayMatch = ScheduleManager::isReadingDay(ptm);
    bool isTimeMatch = (ptm->tm_hour == m_readHourLocal && ptm->tm_min == m_readMinuteLocal);
    bool isSecondMatch = (ptm->tm_sec == 0);

    // Trigger only on the first match (edge detection)
    bool shouldTrigger = isDayMatch && isTimeMatch && isSecondMatch &&
                         (!m_lastReadDayMatch || !m_lastReadTimeMatch);

    m_lastReadDayMatch = isDayMatch && isTimeMatch;
    m_lastReadTimeMatch = isTimeMatch;

    return shouldTrigger;
}

void MeterReader::triggerReading(bool isScheduled)
{
    if (m_readingInProgress)
    {
        Serial.println("[MeterReader] Reading already in progress, skipping trigger");
        return;
    }

    m_isScheduledRead = isScheduled;
    m_readingInProgress = true;

    Serial.printf("[MeterReader] Triggering %s reading...\n", isScheduled ? "scheduled" : "manual");

    performReading();
}

void MeterReader::performReading()
{
    if (!m_publisher->isReady())
    {
        Serial.println("[MeterReader] Publisher not ready, aborting read");
        m_readingInProgress = false;
        return;
    }

    // Publish status
    m_publisher->publishActiveReading(true);
    m_publisher->publishRadioState("Reading");

    // Increment attempt counter
    m_totalReadAttempts++;

    Serial.printf("[MeterReader] Reading attempt %lu (retry %d/%d)\n",
                  m_totalReadAttempts, m_retryCount, m_config->getMaxRetries());

    // Perform actual meter read
    struct tmeter_data meter_data = get_meter_data();

    // Validate data
    if (meter_data.reads_counter == 0 || meter_data.volume == 0)
    {
        handleFailedRead();
        return;
    }

    // Success!
    handleSuccessfulRead(meter_data);
}

void MeterReader::handleSuccessfulRead(const tmeter_data &data)
{
    Serial.println("[MeterReader] Read successful!");

    // Reset retry state
    resetRetryState();

    // Update statistics
    m_successfulReads++;
    m_lastErrorMessage = "None";

    // Get timestamp
    char iso8601[32];
    time_t now = m_timeProvider->getCurrentTime();
    strftime(iso8601, sizeof(iso8601), "%FT%TZ", gmtime(&now));

    // Emit a concise, MQTT-style summary into the ESPHome log
    logReadableSummary(data, m_config);

    // Publish meter data
    m_publisher->publishMeterReading(data, iso8601);

    // Publish historical data if available
    if (data.history_available)
    {
        m_publisher->publishHistory(data.history, true);
    }

    // Publish updated statistics
    m_publisher->publishStatistics(m_totalReadAttempts, m_successfulReads, m_failedReads);

    // Update status
    m_publisher->publishActiveReading(false);
    m_publisher->publishRadioState("Idle");
    m_publisher->publishStatusMessage("Reading successful");

    m_readingInProgress = false;

    Serial.println("[MeterReader] Data published successfully");
}

void MeterReader::handleFailedRead()
{
    Serial.printf("[MeterReader] Read failed (attempt %d/%d)\n",
                  m_retryCount + 1, m_config->getMaxRetries());

    if (m_retryCount < m_config->getMaxRetries() - 1)
    {
        // Schedule retry after delay
        m_retryCount++;
        m_nextRetryTime = millis() + RETRY_DELAY_MS;
        m_lastErrorMessage = "Retrying after failure";

        m_publisher->publishStatusMessage("Retry scheduled");
        m_publisher->publishError(m_lastErrorMessage);
        m_publisher->publishActiveReading(false);
        m_publisher->publishRadioState("Idle");

        m_readingInProgress = false;

        Serial.printf("[MeterReader] Retry %d/%d scheduled in %lu seconds\n",
                      m_retryCount + 1, m_config->getMaxRetries(), RETRY_DELAY_MS / 1000);
    }
    else
    {
        // Max retries reached
        m_failedReads++;
        m_lastFailedAttempt = millis();
        m_lastErrorMessage = "Max retries reached - cooling down";

        m_publisher->publishError(m_lastErrorMessage);
        m_publisher->publishStatusMessage("Failed after max retries");
        m_publisher->publishStatistics(m_totalReadAttempts, m_successfulReads, m_failedReads);
        m_publisher->publishActiveReading(false);
        m_publisher->publishRadioState("Idle");

        resetRetryState();
        m_readingInProgress = false;

        unsigned long cooldownSec = m_config->getRetryCooldownMs() / 1000;
        Serial.printf("[MeterReader] Entering cooldown period (%lu seconds)\n", cooldownSec);
    }
}

void MeterReader::resetRetryState()
{
    m_retryCount = 0;
    m_nextRetryTime = 0;
}

void MeterReader::performFrequencyScan(bool wideRange)
{
    Serial.printf("[MeterReader] Starting %s frequency scan...\n", wideRange ? "wide" : "narrow");

    if (wideRange)
    {
        FrequencyManager::performWideInitialScan();
    }
    else
    {
        FrequencyManager::performFrequencyScan();
    }

    Serial.println("[MeterReader] Frequency scan complete");
}

void MeterReader::getStatistics(unsigned long &totalAttempts, unsigned long &successfulReads,
                                unsigned long &failedReads) const
{
    totalAttempts = m_totalReadAttempts;
    successfulReads = m_successfulReads;
    failedReads = m_failedReads;
}

void MeterReader::setHAConnected(bool connected)
{
    m_haConnected = connected;
}
