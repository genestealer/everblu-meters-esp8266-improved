/**
 * @file esphome_data_publisher.cpp
 * @brief Implementation of ESPHome data publisher
 */

#include "esphome_data_publisher.h"

#ifdef USE_ESPHOME
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif

ESPHomeDataPublisher::ESPHomeDataPublisher()
{
}

void ESPHomeDataPublisher::publishMeterReading(const tmeter_data &data, const char *timestamp)
{
#ifdef USE_ESPHOME
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

    // Wake window times
    if (time_start_sensor_)
    {
        time_start_sensor_->publish_state(data.time_start);
    }

    if (time_end_sensor_)
    {
        time_end_sensor_->publish_state(data.time_end);
    }

    // Timestamp
    if (timestamp_sensor_ && timestamp)
    {
        timestamp_sensor_->publish_state(timestamp);
    }
#endif
}

void ESPHomeDataPublisher::publishHistory(const uint32_t *history, bool historyAvailable)
{
    // History data could be published as:
    // 1. Sensor attributes (not directly supported in ESPHome sensors)
    // 2. Separate sensors for each month (would need 13 additional sensors)
    // 3. Text sensor with JSON (could work)
    //
    // For now, we'll skip history in ESPHome mode since:
    // - ESPHome has built-in statistics/history via InfluxDB integration
    // - Users can use ESPHome's history features
    // - Main readings are what matter most

    // Future: Could add a text sensor with JSON if needed
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

    // Other settings are typically static config in ESPHome
    // No need to publish them as they're in the YAML config
#endif
}

void ESPHomeDataPublisher::publishStatusMessage(const char *message)
{
#ifdef USE_ESPHOME
    if (status_sensor_ && message)
    {
        status_sensor_->publish_state(message);
    }
#endif
}

void ESPHomeDataPublisher::publishRadioState(const char *state)
{
#ifdef USE_ESPHOME
    if (radio_state_sensor_ && state)
    {
        radio_state_sensor_->publish_state(state);
    }

    // Also update binary sensor for radio connection
    if (radio_connected_sensor_)
    {
        bool connected = (strcmp(state, "unavailable") != 0);
        radio_connected_sensor_->publish_state(connected);
    }
#endif
}

void ESPHomeDataPublisher::publishActiveReading(bool active)
{
#ifdef USE_ESPHOME
    if (active_reading_sensor_)
    {
        active_reading_sensor_->publish_state(active);
    }
#endif
}

void ESPHomeDataPublisher::publishError(const char *error)
{
#ifdef USE_ESPHOME
    if (error_sensor_ && error)
    {
        error_sensor_->publish_state(error);
    }
#endif
}

void ESPHomeDataPublisher::publishStatistics(unsigned long totalAttempts, unsigned long successfulReads,
                                             unsigned long failedReads)
{
#ifdef USE_ESPHOME
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

void ESPHomeDataPublisher::publishUptime(unsigned long uptimeSeconds, const char *uptimeISO)
{
#ifdef USE_ESPHOME
    if (uptime_sensor_)
    {
        uptime_sensor_->publish_state(uptimeSeconds);
    }
#endif
}

void ESPHomeDataPublisher::publishFirmwareVersion(const char *version)
{
#ifdef USE_ESPHOME
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
