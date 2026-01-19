/**
 * @file meter_reader.cpp
 * @brief Implementation of MeterReader orchestrator
 */

#include "meter_reader.h"
#include "schedule_manager.h"
#include "meter_history.h"

// Conditional includes based on build environment
#ifdef USE_ESPHOME
#include "utils.h"
#include "wifi_serial.h"
#include "logging.h"
#else
#include "utils.h"
#include "wifi_serial.h"
#include "logging.h"
#endif

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
    LOG_I("everblu_meter", "Initializing...");

    // Register FrequencyManager callbacks
    FrequencyManager::setRadioInitCallback(cc1101_init);
    FrequencyManager::setMeterReadCallback(get_meter_data);

    // Initialize FrequencyManager with configured frequency
    float frequency = m_config->getFrequency();
    FrequencyManager::begin(frequency);
    FrequencyManager::setAutoScanEnabled(m_config->isAutoScanEnabled());

    // Note: Adaptive threshold is set by the platform (ESPHome/MQTT) after this method
    // For MQTT: set via ADAPTIVE_THRESHOLD define in private.h
    // For ESPHome: set via setAdaptiveThreshold() in everblu_meter.cpp

    float effectiveFrequency = frequency + FrequencyManager::getOffset();

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

    LOG_I("everblu_meter", "Scheduled reading time: %02d:%02d UTC (%02d:%02d local)",
          utcHour, utcMinute, m_readHourLocal, m_readMinuteLocal);
    LOG_I("everblu_meter", "Reading schedule: %s", m_config->getReadingSchedule());

    m_initialized = true;
    LOG_I("everblu_meter", "Initialization complete");

#ifdef USE_ESPHOME
    // In ESPHome mode avoid publishing zero/blank states on boot to prevent HA from overwriting restored history.
    if (m_publisher)
    {
        char utc_time_buf[8];
        snprintf(utc_time_buf, sizeof(utc_time_buf), "%02d:%02d", utcHour, utcMinute);
        m_publisher->publishMeterSettings(m_config->getMeterYear(), m_config->getMeterSerial(), m_config->getReadingSchedule(), utc_time_buf, m_config->getFrequency());
    }
#else
    // Standalone/MQTT mode: publish initial states so entities are not Unknown
    if (m_publisher)
    {
        LOG_I("everblu_meter", "Publishing initial sensor states...");

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
        m_publisher->publishFrequencyOffset(FrequencyManager::getOffset());
        m_publisher->publishTunedFrequency(FrequencyManager::getTunedFrequency());

        LOG_I("everblu_meter", "Initial states published");
    }
    else
    {
        LOG_W("everblu_meter", "Publisher not available, cannot publish initial states");
    }
#endif
}

void MeterReader::loop()
{
    if (!m_initialized)
        return;

    unsigned long now = millis();

    // Check for pending retry
    if (m_retryCount > 0 && m_nextRetryTime > 0 && now >= m_nextRetryTime)
    {
        LOG_I("MeterReader", "Retry timer expired, attempting retry %d/%d",
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
            m_publisher->publishFrequencyOffset(FrequencyManager::getOffset());
            m_publisher->publishTunedFrequency(FrequencyManager::getTunedFrequency());
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
        LOG_W("everblu_meter", "Reading already in progress, skipping trigger");
        return;
    }

    m_isScheduledRead = isScheduled;
    m_readingInProgress = true;

    LOG_I("everblu_meter", "Triggering %s reading...", isScheduled ? "scheduled" : "manual");

    performReading();
}

void MeterReader::performReading()
{
    if (!m_publisher->isReady())
    {
        LOG_W("everblu_meter", "Publisher not ready, aborting read");
        m_readingInProgress = false;
        return;
    }

    // Publish status
    m_publisher->publishActiveReading(true);
    m_publisher->publishRadioState("Reading");

    // Increment attempt counter
    m_totalReadAttempts++;

    // Log current radio frequency for diagnostics
    float currentFreq = FrequencyManager::getTunedFrequency();
    float currentOffset = FrequencyManager::getOffset();

    LOG_I("everblu_meter", "Reading attempt %lu (retry %d/%d) at %.6f MHz (offset: %.3f kHz)",
          m_totalReadAttempts, m_retryCount, m_config->getMaxRetries(),
          currentFreq, currentOffset * 1000.0);

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
    LOG_I("everblu_meter", "Read successful!");

    // Reset retry state
    resetRetryState();

    // Update statistics
    m_successfulReads++;
    m_lastErrorMessage = "None";

    // Perform adaptive frequency tracking based on FREQEST register
    FrequencyManager::adaptiveFrequencyTracking(data.freqest);

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
    m_publisher->publishFrequencyOffset(FrequencyManager::getOffset());
    m_publisher->publishTunedFrequency(FrequencyManager::getTunedFrequency());

    // Update status
    m_publisher->publishActiveReading(false);
    m_publisher->publishRadioState("Idle");
    m_publisher->publishStatusMessage("Reading successful");

    m_readingInProgress = false;

    LOG_I("everblu_meter", "Data published successfully");
}

void MeterReader::handleFailedRead()
{
    LOG_W("everblu_meter", "Read failed (attempt %d/%d)",
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

        LOG_I("everblu_meter", "Retry %d/%d scheduled in %lu seconds",
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
        m_publisher->publishFrequencyOffset(FrequencyManager::getOffset());
        m_publisher->publishActiveReading(false);
        m_publisher->publishRadioState("Idle");

        resetRetryState();
        m_readingInProgress = false;

        unsigned long cooldownSec = m_config->getRetryCooldownMs() / 1000;
        LOG_W("everblu_meter", "Entering cooldown period (%lu seconds)", cooldownSec);
    }
}

void MeterReader::resetRetryState()
{
    m_retryCount = 0;
    m_nextRetryTime = 0;
}

void MeterReader::performFrequencyScan(bool wideRange)
{
    LOG_I("everblu_meter", "Starting %s frequency scan...", wideRange ? "wide" : "narrow");

    if (wideRange)
    {
        FrequencyManager::performWideInitialScan();
    }
    else
    {
        FrequencyManager::performFrequencyScan();
    }

    LOG_I("everblu_meter", "Frequency scan complete");

    // Publish the updated frequency offset immediately after scan completes
    if (m_publisher)
    {
        float offsetMHz = FrequencyManager::getOffset();
        m_publisher->publishFrequencyOffset(offsetMHz);
        m_publisher->publishTunedFrequency(FrequencyManager::getTunedFrequency());
    }
}

void MeterReader::resetFrequencyOffset()
{
    LOG_I("everblu_meter", "Resetting frequency offset to 0");

    // Reset offset to 0 and save
    FrequencyManager::saveFrequencyOffset(0.0);

    // Reinitialize radio with base frequency
    float baseFrequency = FrequencyManager::getBaseFrequency();
    cc1101_init(baseFrequency);

    LOG_I("everblu_meter", "Radio reinitialized with base frequency: %.6f MHz", baseFrequency);

    // Publish the reset values
    if (m_publisher)
    {
        m_publisher->publishFrequencyOffset(0.0);
        m_publisher->publishTunedFrequency(baseFrequency);
    }
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
