/**
 * @file esphome_data_publisher.h
 * @brief Data publisher using ESPHome sensor components
 *
 * Publishes meter data directly to ESPHome sensor objects instead of MQTT.
 * ESPHome handles Home Assistant integration via its native API.
 */

#ifndef ESPHOME_DATA_PUBLISHER_H
#define ESPHOME_DATA_PUBLISHER_H

#include "../data_publisher.h"

// Forward declarations for ESPHome components
// These will be available when compiled in ESPHome environment
#ifdef USE_ESPHOME
namespace esphome
{
    namespace sensor
    {
        class Sensor;
    }
    namespace text_sensor
    {
        class TextSensor;
    }
    namespace binary_sensor
    {
        class BinarySensor;
    }
}
#endif

/**
 * @class ESPHomeDataPublisher
 * @brief ESPHome sensor-based data publisher
 *
 * Publishes all meter data and status to ESPHome sensor components.
 * ESPHome then handles Home Assistant integration via its API.
 *
 * This is much simpler than MQTT since:
 * - No topic management needed
 * - No JSON serialization
 * - No Home Assistant discovery messages
 * - Just update sensor states
 */
class ESPHomeDataPublisher : public IDataPublisher
{
public:
    ESPHomeDataPublisher();
    ~ESPHomeDataPublisher() override = default;

#ifdef USE_ESPHOME
    // Sensor registration (called during component setup)
    void set_volume_sensor(esphome::sensor::Sensor *sensor) { volume_sensor_ = sensor; }
    void set_battery_sensor(esphome::sensor::Sensor *sensor) { battery_sensor_ = sensor; }
    void set_counter_sensor(esphome::sensor::Sensor *sensor) { counter_sensor_ = sensor; }
    void set_rssi_sensor(esphome::sensor::Sensor *sensor) { rssi_sensor_ = sensor; }
    void set_rssi_percentage_sensor(esphome::sensor::Sensor *sensor) { rssi_percentage_sensor_ = sensor; }
    void set_lqi_sensor(esphome::sensor::Sensor *sensor) { lqi_sensor_ = sensor; }
    void set_lqi_percentage_sensor(esphome::sensor::Sensor *sensor) { lqi_percentage_sensor_ = sensor; }
    void set_time_start_sensor(esphome::sensor::Sensor *sensor) { time_start_sensor_ = sensor; }
    void set_time_end_sensor(esphome::sensor::Sensor *sensor) { time_end_sensor_ = sensor; }
    void set_frequency_sensor(esphome::sensor::Sensor *sensor) { frequency_sensor_ = sensor; }

    // Statistics sensors
    void set_total_attempts_sensor(esphome::sensor::Sensor *sensor) { total_attempts_sensor_ = sensor; }
    void set_successful_reads_sensor(esphome::sensor::Sensor *sensor) { successful_reads_sensor_ = sensor; }
    void set_failed_reads_sensor(esphome::sensor::Sensor *sensor) { failed_reads_sensor_ = sensor; }
    void set_uptime_sensor(esphome::sensor::Sensor *sensor) { uptime_sensor_ = sensor; }

    // Text sensors
    void set_status_sensor(esphome::text_sensor::TextSensor *sensor) { status_sensor_ = sensor; }
    void set_error_sensor(esphome::text_sensor::TextSensor *sensor) { error_sensor_ = sensor; }
    void set_radio_state_sensor(esphome::text_sensor::TextSensor *sensor) { radio_state_sensor_ = sensor; }
    void set_timestamp_sensor(esphome::text_sensor::TextSensor *sensor) { timestamp_sensor_ = sensor; }
    void set_version_sensor(esphome::text_sensor::TextSensor *sensor) { version_sensor_ = sensor; }

    // Binary sensors
    void set_active_reading_sensor(esphome::binary_sensor::BinarySensor *sensor) { active_reading_sensor_ = sensor; }
    void set_radio_connected_sensor(esphome::binary_sensor::BinarySensor *sensor) { radio_connected_sensor_ = sensor; }
#endif

    // IDataPublisher interface implementation
    void publishMeterReading(const tmeter_data &data, const char *timestamp) override;
    void publishHistory(const uint32_t *history, bool historyAvailable) override;
    void publishWiFiDetails(const char *ip, int rssi, int signalPercent,
                            const char *mac, const char *ssid, const char *bssid) override;
    void publishMeterSettings(int meterYear, unsigned long meterSerial,
                              const char *schedule, const char *readingTime,
                              float frequency) override;
    void publishStatusMessage(const char *message) override;
    void publishRadioState(const char *state) override;
    void publishActiveReading(bool active) override;
    void publishError(const char *error) override;
    void publishStatistics(unsigned long totalAttempts, unsigned long successfulReads,
                           unsigned long failedReads) override;
    void publishUptime(unsigned long uptimeSeconds, const char *uptimeISO) override;
    void publishFirmwareVersion(const char *version) override;
    void publishDiscovery() override;
    bool isReady() const override;

private:
#ifdef USE_ESPHOME
    // Numeric sensors
    esphome::sensor::Sensor *volume_sensor_{nullptr};
    esphome::sensor::Sensor *battery_sensor_{nullptr};
    esphome::sensor::Sensor *counter_sensor_{nullptr};
    esphome::sensor::Sensor *rssi_sensor_{nullptr};
    esphome::sensor::Sensor *rssi_percentage_sensor_{nullptr};
    esphome::sensor::Sensor *lqi_sensor_{nullptr};
    esphome::sensor::Sensor *lqi_percentage_sensor_{nullptr};
    esphome::sensor::Sensor *time_start_sensor_{nullptr};
    esphome::sensor::Sensor *time_end_sensor_{nullptr};
    esphome::sensor::Sensor *frequency_sensor_{nullptr};

    // Statistics sensors
    esphome::sensor::Sensor *total_attempts_sensor_{nullptr};
    esphome::sensor::Sensor *successful_reads_sensor_{nullptr};
    esphome::sensor::Sensor *failed_reads_sensor_{nullptr};
    esphome::sensor::Sensor *uptime_sensor_{nullptr};

    // Text sensors
    esphome::text_sensor::TextSensor *status_sensor_{nullptr};
    esphome::text_sensor::TextSensor *error_sensor_{nullptr};
    esphome::text_sensor::TextSensor *radio_state_sensor_{nullptr};
    esphome::text_sensor::TextSensor *timestamp_sensor_{nullptr};
    esphome::text_sensor::TextSensor *version_sensor_{nullptr};

    // Binary sensors
    esphome::binary_sensor::BinarySensor *active_reading_sensor_{nullptr};
    esphome::binary_sensor::BinarySensor *radio_connected_sensor_{nullptr};
#endif

    // Helper methods
    int calculateRssiPercentage(int rssi_dbm) const;
    int calculateLqiPercentage(int lqi) const;
};

#endif // ESPHOME_DATA_PUBLISHER_H
