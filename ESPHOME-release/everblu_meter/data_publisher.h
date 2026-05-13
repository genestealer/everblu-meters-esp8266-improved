/**
 * @file data_publisher.h
 * @brief Abstract interface for publishing meter data
 *
 * Provides a platform-agnostic way to publish meter readings and status updates.
 * This allows the core meter reading logic to work with different backends
 * (MQTT, ESPHome sensors, HTTP API, etc.) without knowing implementation details.
 *
 * Implementations:
 * - MQTTDataPublisher: Publishes to MQTT broker (standalone mode)
 * - ESPHomeDataPublisher: Updates ESPHome sensor components (ESPHome mode)
 */

#ifndef DATA_PUBLISHER_H
#define DATA_PUBLISHER_H

#include "../core/cc1101.h"
#include <stdint.h>

/**
 * @class IDataPublisher
 * @brief Abstract interface for publishing meter data and status
 *
 * All data publishing should go through this interface to enable
 * different backends (MQTT, ESPHome, logging, etc.)
 */
class IDataPublisher
{
public:
    virtual ~IDataPublisher() = default;

    /**
     * @brief Publish complete meter reading
     * @param data Meter data structure with all readings
     * @param timestamp ISO8601 timestamp string
     */
    virtual void publishMeterReading(const tmeter_data &data, const char *timestamp) = 0;

    /**
     * @brief Publish meter historical data
     * @param history Array of 13 monthly readings
     * @param historyAvailable Whether history was successfully decoded
     */
    virtual void publishHistory(const uint32_t *history, bool historyAvailable) = 0;

    /**
     * @brief Publish WiFi connection details
     * @param ip IP address string
     * @param rssi WiFi RSSI in dBm
     * @param signalPercent WiFi signal as percentage
     * @param mac MAC address string
     * @param ssid WiFi SSID
     * @param bssid Access point BSSID
     */
    virtual void publishWiFiDetails(const char *ip, int rssi, int signalPercent,
                                    const char *mac, const char *ssid, const char *bssid) = 0;

    /**
     * @brief Publish meter settings/configuration
     * @param meterYear Meter year (2-digit)
     * @param meterSerial Meter serial number
     * @param schedule Reading schedule string
     * @param readingTime Reading time formatted as HH:MM
     * @param frequency RF frequency in MHz
     */
    virtual void publishMeterSettings(int meterYear, unsigned long meterSerial,
                                      const char *schedule, const char *readingTime,
                                      float frequency) = 0;

    /**
     * @brief Publish status message
     * @param message Status message text
     */
    virtual void publishStatusMessage(const char *message) = 0;

    /**
     * @brief Publish CC1101 radio state
     * @param state State string (e.g., "Idle", "Reading", "unavailable")
     */
    virtual void publishRadioState(const char *state) = 0;

    /**
     * @brief Publish active reading flag
     * @param active true if reading is in progress
     */
    virtual void publishActiveReading(bool active) = 0;

    /**
     * @brief Publish error message
     * @param error Error message text
     */
    virtual void publishError(const char *error) = 0;

    /**
     * @brief Publish read statistics
     * @param totalAttempts Total read attempts
     * @param successfulReads Successful reads
     * @param failedReads Failed reads
     */
    virtual void publishStatistics(unsigned long totalAttempts, unsigned long successfulReads,
                                   unsigned long failedReads) = 0;

    /**
     * @brief Publish frequency offset
     * @param offsetMHz Frequency offset in MHz
     */
    virtual void publishFrequencyOffset(float offsetMHz) = 0;

    /**
     * @brief Publish tuned frequency (base frequency + offset)
     * @param frequencyMHz Actual tuned frequency in MHz with offset applied
     */
    virtual void publishTunedFrequency(float frequencyMHz) = 0;

    /**
     * @brief Publish frequency estimate from CC1101
     * @param freqestValue Raw FREQEST value from meter reading (-128 to +127)
     */
    virtual void publishFrequencyEstimate(int8_t freqestValue) = 0;

    /**
     * @brief Publish uptime
     * @param uptimeSeconds System uptime in seconds
     * @param uptimeISO Uptime as ISO8601 duration string
     */
    virtual void publishUptime(unsigned long uptimeSeconds, const char *uptimeISO) = 0;

    /**
     * @brief Publish firmware version
     * @param version Firmware version string
     */
    virtual void publishFirmwareVersion(const char *version) = 0;

    /**
     * @brief Publish Home Assistant MQTT discovery messages
     *
     * For ESPHome mode, this would be a no-op since ESPHome handles discovery
     */
    virtual void publishDiscovery() = 0;

    /**
     * @brief Check if publisher is ready/connected
     * @return true if ready to publish data
     */
    virtual bool isReady() const = 0;
};

#endif // DATA_PUBLISHER_H
