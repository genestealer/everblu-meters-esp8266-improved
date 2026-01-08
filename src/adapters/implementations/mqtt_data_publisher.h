/**
 * @file mqtt_data_publisher.h
 * @brief Data publisher using MQTT for standalone mode
 *
 * Publishes meter data to MQTT broker with Home Assistant discovery support.
 */

#ifndef MQTT_DATA_PUBLISHER_H
#define MQTT_DATA_PUBLISHER_H

#include "../data_publisher.h"
#include <EspMQTTClient.h>

/**
 * @class MQTTDataPublisher
 * @brief MQTT-based data publisher for standalone mode
 *
 * Publishes all meter data and status to MQTT broker.
 * Supports Home Assistant MQTT Discovery protocol.
 */
class MQTTDataPublisher : public IDataPublisher
{
public:
    /**
     * @brief Constructor
     * @param mqttClient Reference to MQTT client instance
     * @param baseTopic MQTT base topic (e.g., "everblu/cyble/1234567")
     * @param meterIsGas true if meter is gas type
     * @param gasVolumeDivisor Divisor for gas readings
     */
    MQTTDataPublisher(EspMQTTClient &mqttClient, const char *baseTopic,
                      bool meterIsGas, int gasVolumeDivisor);
    ~MQTTDataPublisher() override = default;

    // IDataPublisher interface
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
    /**
     * @brief Publish to MQTT topic (with base topic prefix)
     * @param subtopic Subtopic (will be appended to base)
     * @param payload Payload string
     * @param retained Retained flag
     */
    void publish(const char *subtopic, const char *payload, bool retained = true);

    /**
     * @brief Build discovery JSON for a sensor
     * @param name Sensor name
     * @param deviceClass Device class (e.g., "energy", "battery")
     * @param stateTopic State topic
     * @param unitOfMeasurement Unit string
     * @param icon Icon name
     * @param valuePath JSON value path
     * @param stateClass State class (e.g., "total_increasing")
     * @return Discovery JSON string
     */
    String buildDiscoveryJson(const char *name, const char *deviceClass,
                              const char *stateTopic, const char *unitOfMeasurement,
                              const char *icon, const char *valuePath = nullptr,
                              const char *stateClass = nullptr);

    EspMQTTClient &m_mqtt;
    const char *m_baseTopic;
    bool m_meterIsGas;
    int m_gasVolumeDivisor;

    static const int TOPIC_BUFFER_SIZE = 128;
    static const int PAYLOAD_BUFFER_SIZE = 256;
};

#endif // MQTT_DATA_PUBLISHER_H
