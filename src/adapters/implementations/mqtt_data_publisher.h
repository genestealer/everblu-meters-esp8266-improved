/**
 * @file mqtt_data_publisher.h
 * @brief Data publisher using MQTT for standalone mode
 *
 * Publishes meter data to MQTT broker with Home Assistant discovery support.
 */

#ifndef MQTT_DATA_PUBLISHER_H
#define MQTT_DATA_PUBLISHER_H

#include "../data_publisher.h"

// Build the real publisher only when NOT using ESPHome and the library exists
#if !defined(USE_ESPHOME) && (__has_include(<EspMQTTClient.h>))
#include <EspMQTTClient.h>

class MQTTDataPublisher : public IDataPublisher
{
public:
    MQTTDataPublisher(EspMQTTClient &mqttClient, const char *baseTopic,
                      bool meterIsGas, int gasVolumeDivisor);
    ~MQTTDataPublisher() override = default;

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
    void publish(const char *subtopic, const char *payload, bool retained = true);

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

#else

// Stubbed implementation for ESPHome or when EspMQTTClient is unavailable
class EspMQTTClient; // forward declaration to satisfy constructor signature

class MQTTDataPublisher : public IDataPublisher
{
public:
    MQTTDataPublisher(EspMQTTClient &, const char *, bool, int) {}
    ~MQTTDataPublisher() override = default;

    void publishMeterReading(const tmeter_data &, const char *) override {}
    void publishHistory(const uint32_t *, bool) override {}
    void publishWiFiDetails(const char *, int, int, const char *, const char *, const char *) override {}
    void publishMeterSettings(int, unsigned long, const char *, const char *, float) override {}
    void publishStatusMessage(const char *) override {}
    void publishRadioState(const char *) override {}
    void publishActiveReading(bool) override {}
    void publishError(const char *) override {}
    void publishStatistics(unsigned long, unsigned long, unsigned long) override {}
    void publishUptime(unsigned long, const char *) override {}
    void publishFirmwareVersion(const char *) override {}
    void publishDiscovery() override {}
    bool isReady() const override { return false; }

private:
    void publish(const char *, const char *, bool = true) {}
};

#endif // real vs stub

#endif // MQTT_DATA_PUBLISHER_H
