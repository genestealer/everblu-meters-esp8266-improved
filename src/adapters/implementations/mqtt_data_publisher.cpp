/**
 * @file mqtt_data_publisher.cpp
 * @brief Implementation of MQTT data publisher for standalone mode
 */

#if !defined(USE_ESPHOME) && (__has_include(<EspMQTTClient.h>))
#include "mqtt_data_publisher.h"
#include "../../core/utils.h"
#include "../../core/version.h"
#include <Arduino.h>

// Note: calculateMeterdBmToPercentage() and calculateLQIToPercentage() are now in utils.h/cpp

MQTTDataPublisher::MQTTDataPublisher(EspMQTTClient &mqttClient, const char *baseTopic,
                                     bool meterIsGas, int gasVolumeDivisor)
    : m_mqtt(mqttClient), m_baseTopic(baseTopic), m_meterIsGas(meterIsGas), m_gasVolumeDivisor(gasVolumeDivisor)
{
}

void MQTTDataPublisher::publish(const char *subtopic, const char *payload, bool retained)
{
    char topic[TOPIC_BUFFER_SIZE];
    snprintf(topic, sizeof(topic), "%s/%s", m_baseTopic, subtopic);
    m_mqtt.publish(topic, payload, retained);
}

void MQTTDataPublisher::publishMeterReading(const tmeter_data &data, const char *timestamp)
{
    char buffer[PAYLOAD_BUFFER_SIZE];

    // Publish volume (liters for water, cubic meters for gas)
    if (m_meterIsGas)
    {
        float cubicMeters = data.volume / (float)m_gasVolumeDivisor;
        snprintf(buffer, sizeof(buffer), "%.3f", cubicMeters);
    }
    else
    {
        snprintf(buffer, sizeof(buffer), "%d", data.volume);
    }
    publish("liters", buffer, true);

    // Publish battery
    snprintf(buffer, sizeof(buffer), "%d", data.battery_left);
    publish("battery", buffer, true);

    // Publish counter
    snprintf(buffer, sizeof(buffer), "%d", data.reads_counter);
    publish("counter", buffer, true);

    // Publish RSSI
    snprintf(buffer, sizeof(buffer), "%d", data.rssi_dbm);
    publish("rssi", buffer, true);

    // Publish RSSI percentage
    int rssiPercent = calculateMeterdBmToPercentage(data.rssi_dbm);
    snprintf(buffer, sizeof(buffer), "%d", rssiPercent);
    publish("rssi_percentage", buffer, true);

    // Publish LQI
    snprintf(buffer, sizeof(buffer), "%d", data.lqi);
    publish("lqi", buffer, true);

    // Publish LQI percentage
    int lqiPercent = calculateLQIToPercentage(data.lqi);
    snprintf(buffer, sizeof(buffer), "%d", lqiPercent);
    publish("lqi_percentage", buffer, true);

    // Publish wake window
    snprintf(buffer, sizeof(buffer), "%02d:00", constrain(data.time_start, 0, 23));
    publish("time_window_start", buffer, true);

    snprintf(buffer, sizeof(buffer), "%02d:00", constrain(data.time_end, 0, 23));
    publish("time_window_end", buffer, true);

    // Publish timestamp
    publish("last_update", timestamp, true);

    // Publish frequency offset
    snprintf(buffer, sizeof(buffer), "%d", data.freqest);
    publish("frequency_offset", buffer, true);
}

void MQTTDataPublisher::publishHistory(const uint32_t *history, bool historyAvailable)
{
    if (!historyAvailable || !history)
    {
        publish("history_available", "false", true);
        return;
    }

    publish("history_available", "true", true);

    // Build JSON array of historical data
    String historyJson = "[";
    for (int i = 0; i < 13; i++)
    {
        if (i > 0)
            historyJson += ",";
        historyJson += String(history[i]);
    }
    historyJson += "]";

    publish("history", historyJson.c_str(), true);
}

void MQTTDataPublisher::publishWiFiDetails(const char *ip, int rssi, int signalPercent,
                                           const char *mac, const char *ssid, const char *bssid)
{
    char buffer[PAYLOAD_BUFFER_SIZE];

    publish("wifi_ip", ip, true);

    snprintf(buffer, sizeof(buffer), "%d", rssi);
    publish("wifi_rssi", buffer, true);

    snprintf(buffer, sizeof(buffer), "%d", signalPercent);
    publish("wifi_signal_percentage", buffer, true);

    publish("mac_address", mac, true);
    publish("wifi_ssid", ssid, true);
    publish("wifi_bssid", bssid, true);
    publish("status", "online", true);
}

void MQTTDataPublisher::publishMeterSettings(int meterYear, unsigned long meterSerial,
                                             const char *schedule, const char *readingTime,
                                             float frequency)
{
    char buffer[PAYLOAD_BUFFER_SIZE];

    snprintf(buffer, sizeof(buffer), "%d", meterYear);
    publish("everblu_meter_year", buffer, true);

    snprintf(buffer, sizeof(buffer), "%lu", meterSerial);
    publish("everblu_meter_serial", buffer, true);

    publish("reading_schedule", schedule, true);
    publish("reading_time", readingTime, true);

    snprintf(buffer, sizeof(buffer), "%.3f", frequency);
    publish("frequency", buffer, true);
}

void MQTTDataPublisher::publishStatusMessage(const char *message)
{
    publish("status_message", message, true);
}

void MQTTDataPublisher::publishRadioState(const char *state)
{
    publish("cc1101_state", state, true);
}

void MQTTDataPublisher::publishActiveReading(bool active)
{
    publish("active_reading", active ? "true" : "false", true);
}

void MQTTDataPublisher::publishError(const char *error)
{
    publish("last_error", error, true);
}

void MQTTDataPublisher::publishStatistics(unsigned long totalAttempts, unsigned long successfulReads,
                                          unsigned long failedReads)
{
    char buffer[32];

    snprintf(buffer, sizeof(buffer), "%lu", totalAttempts);
    publish("total_attempts", buffer, true);

    snprintf(buffer, sizeof(buffer), "%lu", successfulReads);
    publish("successful_reads", buffer, true);

    snprintf(buffer, sizeof(buffer), "%lu", failedReads);
    publish("failed_reads", buffer, true);
}

void MQTTDataPublisher::publishFrequencyOffset(float offsetMHz)
{
    char buffer[16];
    // Convert MHz to kHz to match MQTT discovery
    snprintf(buffer, sizeof(buffer), "%.3f", offsetMHz * 1000.0f);
    publish("frequency_offset", buffer, true);
}

void MQTTDataPublisher::publishTunedFrequency(float frequencyMHz)
{
    char buffer[16];
    // Publish in MHz with high precision (4 decimal places = kHz resolution)
    snprintf(buffer, sizeof(buffer), "%.6f", frequencyMHz);
    publish("tuned_frequency", buffer, true);
}

void MQTTDataPublisher::publishFrequencyEstimate(int8_t freqestValue)
{
    char buffer[16];
    // Convert FREQEST raw value to kHz (approximately 1.59 kHz per LSB with 26 MHz crystal)
    constexpr float FREQEST_TO_KHZ = 1.587; // ~1.59 kHz per LSB
    float freqestKHz = (float)freqestValue * FREQEST_TO_KHZ;
    snprintf(buffer, sizeof(buffer), "%.3f", freqestKHz);
    publish("frequency_estimate", buffer, true);
}

void MQTTDataPublisher::publishUptime(unsigned long uptimeSeconds, const char *uptimeISO)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%lu", uptimeSeconds);
    publish("uptime", buffer, true);
}

void MQTTDataPublisher::publishFirmwareVersion(const char *version)
{
    publish("firmware_version", version, true);
}

void MQTTDataPublisher::publishDiscovery()
{
    // Note: For a full implementation, this would publish all Home Assistant
    // MQTT discovery messages. Keeping this simplified for now.
    Serial.println("[MQTTPublisher] Home Assistant discovery messages would be published here");

    // The original code has extensive discovery logic in main.cpp
    // We can migrate that here if needed, but for now this is a placeholder
}

bool MQTTDataPublisher::isReady() const
{
    return m_mqtt.isMqttConnected();
}

String MQTTDataPublisher::buildDiscoveryJson(const char *name, const char *deviceClass,
                                             const char *stateTopic, const char *unitOfMeasurement,
                                             const char *icon, const char *valuePath,
                                             const char *stateClass)
{
    String json = "{\n";
    json += "  \"name\": \"" + String(name) + "\",\n";

    if (deviceClass && strlen(deviceClass) > 0)
    {
        json += "  \"device_class\": \"" + String(deviceClass) + "\",\n";
    }

    json += "  \"state_topic\": \"" + String(stateTopic) + "\",\n";

    if (unitOfMeasurement && strlen(unitOfMeasurement) > 0)
    {
        json += "  \"unit_of_measurement\": \"" + String(unitOfMeasurement) + "\",\n";
    }

    if (icon && strlen(icon) > 0)
    {
        json += "  \"icon\": \"" + String(icon) + "\",\n";
    }

    if (valuePath && strlen(valuePath) > 0)
    {
        json += "  \"value_template\": \"{{ " + String(valuePath) + " }}\",\n";
    }

    if (stateClass && strlen(stateClass) > 0)
    {
        json += "  \"state_class\": \"" + String(stateClass) + "\",\n";
    }

    json += "  \"availability_topic\": \"" + String(m_baseTopic) + "/status\",\n";
    json += "  \"unique_id\": \"everblu_" + String(name) + "\"\n";
    json += "}";

    return json;
}

#endif // build when not ESPHome and EspMQTTClient is available
