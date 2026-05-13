/**
 * @file esphome_data_publisher.cpp
 * @brief Implementation of ESPHome data publisher
 */

#include "esphome_data_publisher.h"
#include "../../services/meter_history.h"

#ifdef USE_ESPHOME
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/log.h"
static const char *const TAG_PUB = "everblu_publisher";
#endif

ESPHomeDataPublisher::ESPHomeDataPublisher()
{
}

void ESPHomeDataPublisher::publishMeterReading(const tmeter_data &data, const char *timestamp)
{
#ifdef USE_ESPHOME
    ESP_LOGD(TAG_PUB, "Publishing meter reading: volume=%lu, battery=%.1f, counter=%lu", (unsigned long)data.volume, (double)data.battery_left, (unsigned long)data.reads_counter);
    have_last_volume_ = true;
    last_volume_ = data.volume;
    // Publish main meter reading
    if (volume_sensor_)
    {
        volume_sensor_->publish_state(data.volume);
    }

    if (battery_sensor_)
    {
        battery_sensor_->publish_state(data.battery_left);
    }

    if (counter_sensor_)
    {
        counter_sensor_->publish_state(data.reads_counter);
    }

    // Signal quality metrics
    if (rssi_sensor_)
    {
        rssi_sensor_->publish_state(data.rssi_dbm);
    }

    if (rssi_percentage_sensor_)
    {
        rssi_percentage_sensor_->publish_state(calculateRssiPercentage(data.rssi_dbm));
    }

    if (lqi_sensor_)
    {
        lqi_sensor_->publish_state(data.lqi);
    }

    if (lqi_percentage_sensor_)
    {
        lqi_percentage_sensor_->publish_state(calculateLqiPercentage(data.lqi));
    }

    // Wake window times (formatted as HH:MM)
    if (time_start_sensor_)
    {
        char time_start_str[6];
        snprintf(time_start_str, sizeof(time_start_str), "%02d:00", data.time_start);
        time_start_sensor_->publish_state(time_start_str);
    }

    if (time_end_sensor_)
    {
        char time_end_str[6];
        snprintf(time_end_str, sizeof(time_end_str), "%02d:00", data.time_end);
        time_end_sensor_->publish_state(time_end_str);
    }

    // Timestamp
    if (timestamp_sensor_ && timestamp)
    {
        timestamp_sensor_->publish_state(timestamp);
    }

    // Frequency estimate from CC1101
    if (frequency_estimate_sensor_)
    {
        publishFrequencyEstimate(data.freqest);
    }
#endif
}

void ESPHomeDataPublisher::publishHistory(const uint32_t *history, bool historyAvailable)
{
#ifdef USE_ESPHOME
    if (!history_sensor_)
    {
        return; // Not configured
    }

    if (!historyAvailable || history == nullptr)
    {
        history_sensor_->publish_state("unavailable");
        return;
    }

    // Build a JSON payload matching the legacy MQTT attributes
    char history_json[512];
    const uint32_t current_volume = have_last_volume_ ? last_volume_ : 0;
    int written = MeterHistory::generateHistoryJson(history, current_volume, history_json, sizeof(history_json));

    if (written <= 0)
    {
        ESP_LOGW(TAG_PUB, "History JSON generation failed (buffer=%u)", (unsigned)sizeof(history_json));
        history_sensor_->publish_state("unavailable");
        return;
    }

    ESP_LOGD(TAG_PUB, "Publishing history JSON (%d bytes)", written);
    history_sensor_->publish_state(history_json);

    // Mirror the legacy serial dump so users can see history in ESPHome logs
    const int month_count = MeterHistory::countValidMonths(history);
    if (month_count > 0)
    {
        MeterHistory::printToSerial(history, current_volume, "[HISTORY]");
    }
#endif
}

void ESPHomeDataPublisher::publishWiFiDetails(const char *ip, int rssi, int signalPercent,
                                              const char *mac, const char *ssid, const char *bssid)
{
    // No-op: ESPHome core handles WiFi status reporting automatically
    // Available via ESPHome's built-in WiFi component sensors
}

void ESPHomeDataPublisher::publishMeterSettings(int meterYear, unsigned long meterSerial,
                                                const char *schedule, const char *readingTime,
                                                float frequency)
{
#ifdef USE_ESPHOME
    // Publish frequency (useful for monitoring calibration)
    if (frequency_sensor_)
    {
        frequency_sensor_->publish_state(frequency);
    }

    // Publish diagnostic settings so HA sees configured values
    if (meter_serial_sensor_)
    {
        char serial_buf[16];
        snprintf(serial_buf, sizeof(serial_buf), "%lu", meterSerial);
        meter_serial_sensor_->publish_state(serial_buf);
    }

    if (meter_year_sensor_)
    {
        char year_buf[8];
        snprintf(year_buf, sizeof(year_buf), "%02d", meterYear);
        meter_year_sensor_->publish_state(year_buf);
    }

    if (reading_schedule_sensor_ && schedule)
    {
        reading_schedule_sensor_->publish_state(schedule);
    }

    if (reading_time_utc_sensor_ && readingTime)
    {
        reading_time_utc_sensor_->publish_state(readingTime);
    }

    // Other settings are typically static config in ESPHome
    // No need to publish them as they're in the YAML config
#endif
}

void ESPHomeDataPublisher::publishStatusMessage(const char *message)
{
#ifdef USE_ESPHOME
    if (!message)
        return;

    if (status_sensor_)
    {
        ESP_LOGD(TAG_PUB, "Publishing status: %s", message);
        status_sensor_->publish_state(message);
    }
    else
    {
        ESP_LOGW(TAG_PUB, "Status sensor not configured, cannot publish: %s", message);
    }
#endif
}

void ESPHomeDataPublisher::publishRadioState(const char *state)
{
#ifdef USE_ESPHOME
    ESP_LOGD(TAG_PUB, "Radio state: %s", state ? state : "(null)");
    if (radio_state_sensor_ && state)
    {
        radio_state_sensor_->publish_state(state);
    }

    // Also update binary sensor for radio connection
    if (radio_connected_sensor_)
    {
        bool connected = (strcmp(state, "unavailable") != 0);
        ESP_LOGD(TAG_PUB, "Publishing radio_connected: %s", connected ? "true" : "false");
        radio_connected_sensor_->publish_state(connected);
    }
    else
    {
        ESP_LOGW(TAG_PUB, "radio_connected_sensor not configured");
    }
#endif
}

void ESPHomeDataPublisher::publishActiveReading(bool active)
{
#ifdef USE_ESPHOME
    ESP_LOGD(TAG_PUB, "Active reading: %s", active ? "true" : "false");
    if (active_reading_sensor_)
    {
        active_reading_sensor_->publish_state(active);
    }
#endif
}

void ESPHomeDataPublisher::publishError(const char *error)
{
#ifdef USE_ESPHOME
    if (!error)
        return;

    if (error_sensor_)
    {
        ESP_LOGD(TAG_PUB, "Publishing error: %s", error);
        error_sensor_->publish_state(error);
    }
    else
    {
        ESP_LOGW(TAG_PUB, "Error sensor not configured, cannot publish: %s", error);
    }
#endif
}

void ESPHomeDataPublisher::publishStatistics(unsigned long totalAttempts, unsigned long successfulReads,
                                             unsigned long failedReads)
{
#ifdef USE_ESPHOME
    ESP_LOGD(TAG_PUB, "Publishing stats: total=%lu success=%lu failed=%lu", totalAttempts, successfulReads, failedReads);

    if (total_attempts_sensor_)
    {
        total_attempts_sensor_->publish_state(totalAttempts);
    }

    if (successful_reads_sensor_)
    {
        successful_reads_sensor_->publish_state(successfulReads);
    }

    if (failed_reads_sensor_)
    {
        failed_reads_sensor_->publish_state(failedReads);
    }
#endif
}

void ESPHomeDataPublisher::publishFrequencyOffset(float offsetMHz)
{
#ifdef USE_ESPHOME
    if (frequency_offset_sensor_)
    {
        // Convert MHz to kHz like MQTT version
        float offsetKHz = offsetMHz * 1000.0f;
        ESP_LOGD(TAG_PUB, "Publishing frequency offset: %.3f kHz", offsetKHz);
        frequency_offset_sensor_->publish_state(offsetKHz);
    }
#endif
}

void ESPHomeDataPublisher::publishTunedFrequency(float frequencyMHz)
{
#ifdef USE_ESPHOME
    if (tuned_frequency_sensor_)
    {
        // Publish in MHz with high precision (6 decimal places = kHz resolution)
        ESP_LOGD(TAG_PUB, "Publishing tuned frequency: %.6f MHz", frequencyMHz);
        tuned_frequency_sensor_->publish_state(frequencyMHz);
    }
#endif
}

void ESPHomeDataPublisher::publishFrequencyEstimate(int8_t freqestValue)
{
#ifdef USE_ESPHOME
    if (frequency_estimate_sensor_)
    {
        // Convert FREQEST raw value to kHz (approximately 1.59 kHz per LSB with 26 MHz crystal)
        constexpr float FREQEST_TO_KHZ = 1.587; // ~1.59 kHz per LSB
        float freqestKHz = (float)freqestValue * FREQEST_TO_KHZ;
        ESP_LOGD(TAG_PUB, "Publishing frequency estimate: %d (%.3f kHz)", freqestValue, freqestKHz);
        frequency_estimate_sensor_->publish_state(freqestKHz);
    }
#endif
}

void ESPHomeDataPublisher::publishUptime(unsigned long uptimeSeconds, const char *uptimeISO)
{
#ifdef USE_ESPHOME
    ESP_LOGD(TAG_PUB, "Uptime: %lu s", uptimeSeconds);
    if (uptime_sensor_)
    {
        uptime_sensor_->publish_state(uptimeSeconds);
    }
#endif
}

void ESPHomeDataPublisher::publishFirmwareVersion(const char *version)
{
#ifdef USE_ESPHOME
    ESP_LOGD(TAG_PUB, "Version: %s", version ? version : "(null)");
    if (version_sensor_ && version)
    {
        version_sensor_->publish_state(version);
    }
#endif
}

void ESPHomeDataPublisher::publishDiscovery()
{
    // No-op: ESPHome handles Home Assistant discovery automatically via its API
    // No need for manual MQTT discovery messages
}

bool ESPHomeDataPublisher::isReady() const
{
    // ESPHome sensors are always "ready" once configured
    return true;
}

// Helper methods
int ESPHomeDataPublisher::calculateRssiPercentage(int rssi_dbm) const
{
    // RSSI to percentage conversion
    // -50 dBm = 100%, -120 dBm = 0%
    int clamped = (rssi_dbm < -120) ? -120 : (rssi_dbm > -50 ? -50 : rssi_dbm);
    return (int)((clamped + 120) * 100.0 / 70.0);
}

int ESPHomeDataPublisher::calculateLqiPercentage(int lqi) const
{
    // LQI is 0-255, convert to percentage
    int clamped = (lqi < 0) ? 0 : (lqi > 255 ? 255 : lqi);
    return (int)(clamped * 100.0 / 255.0);
}
