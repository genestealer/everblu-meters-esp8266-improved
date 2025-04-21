/*
 * Project: EverBlu Meters ESP8266/ESP32
 * Description: Fetch water/gas usage data from Itron EverBlu Cyble Enhanced RF water meters using the RADIAN protocol on 433 MHz.
 *              Integrated with Home Assistant via MQTT AutoDiscovery.
 * Author: Forked and improved by Genestealer, based on work by Psykokwak and Neutrinus.
 * License: Unknown (refer to the README for details).
 * 
 * Hardware Requirements:
 * - ESP8266/ESP32
 * - CC1101 RF Transceiver
 * 
 * Features:
 * - MQTT integration for Home Assistant
 * - Frequency discovery for meter communication
 * - Wi-Fi diagnostics and OTA updates
 * - Daily scheduled meter readings
 * 
 * For more details, refer to the README file.
 */

#include "private.h"        // Include private configuration (Wi-Fi, MQTT, etc.)
#include "everblu_meters.h" // Include EverBlu meter communication library
#include <ESP8266WiFi.h>    // Wi-Fi library for ESP8266
#include <ESP8266mDNS.h>    // mDNS library for ESP8266
#include <Arduino.h>        // Core Arduino library
#include <ArduinoOTA.h>     // OTA update library
#include <EspMQTTClient.h>  // MQTT client library

// Define the LED_BUILTIN pin if missing
#ifndef LED_BUILTIN
#define LED_BUILTIN 2 // Change this pin if needed
#endif

// Define the Wi-Fi PHY mode if missing from the private.h file
#ifndef ENABLE_WIFI_PHY_MODE_11G
#define ENABLE_WIFI_PHY_MODE_11G 0  // Set to 1 to enable 11G PHY mode
#endif

// Define the 433MHZ scan mode if missing from the private.h file
#ifndef SCAN_FREQUENCY_433MHZ
#define SCAN_FREQUENCY_433MHZ 0 // Set to 1 to enable frequency scanning
#endif

// Define the default reading schedule if missing from the private.h file.
// Options: "Monday-Friday", "Monday-Saturday", or "Monday-Sunday"
#ifndef DEFAULT_READING_SCHEDULE
#define DEFAULT_READING_SCHEDULE "Monday-Friday"
#endif

unsigned long lastWifiUpdate = 0;

// Secrets pulled from private.h file
EspMQTTClient mqtt(
    secret_wifi_ssid,     // Your Wifi SSID
    secret_wifi_password, // Your WiFi key
    secret_mqtt_server,   // MQTT Broker server ip
    secret_mqtt_username, // MQTT Username Can be omitted if not needed
    secret_mqtt_password, // MQTT Password Can be omitted if not needed
    secret_clientName,    // MQTT Client name that uniquely identify your device
    1883                  // MQTT Broker server port
);

const char jsonTemplate[] = "{ "
                            "\"liters\": %d, "
                            "\"counter\" : %d, "
                            "\"battery\" : %d, "
                            "\"rssi\" : %d, "
                            "\"timestamp\" : \"%s\" }";

int _retry = 0;

// Global variable to store the reading schedule (default from private.h)
String readingSchedule = DEFAULT_READING_SCHEDULE;

// Function to check if today is within the configured schedule
bool isReadingDay(struct tm *ptm) {
  if (readingSchedule == "Monday-Friday") {
    return ptm->tm_wday >= 1 && ptm->tm_wday <= 5; // Monday to Friday
  } else if (readingSchedule == "Monday-Saturday") {
    return ptm->tm_wday >= 1 && ptm->tm_wday <= 6; // Monday to Saturday
  } else if (readingSchedule == "Monday-Sunday") {
    return true; // Every day
  }
  return false;
}

// Function to scan for the correct frequency in the 433 MHz range
void scanFrequency433MHz() {
  Serial.printf("###### FREQUENCY DISCOVERY ENABLED (433 MHz) ######\nStarting Frequency Scan...\n");
  for (float i = 433.76f; i < 433.890f; i += 0.0005f) {
      Serial.printf("Test frequency : %f\n", i);
      cc1101_init(i);
      struct tmeter_data meter_data = get_meter_data();
      if (meter_data.reads_counter != 0 || meter_data.liters != 0) {
          Serial.printf("\n------------------------------\nGot frequency : %f\n------------------------------\n", i);
          Serial.printf("Liters : %d\nBattery (in months) : %d\nCounter : %d\n\n", meter_data.liters, meter_data.battery_left, meter_data.reads_counter);
          digitalWrite(LED_BUILTIN, LOW); // turned on
          while (42); // Stop execution once frequency is found
      }
  }
  Serial.printf("###### FREQUENCY DISCOVERY FINISHED (433 MHz) ######\nOnce you have discovered the correct frequency you can disable this scan.\n\n");
}


// Function: calculateWiFiSignalStrengthPercentage
// Description: Converts RSSI to a percentage value (0-100%).
int calculateWiFiSignalStrengthPercentage(int rssi) {
  int strength = constrain(rssi, -100, -50); // Clamp RSSI to a reasonable range
  return map(strength, -100, -50, 0, 100);   // Map RSSI to percentage (0-100%)
}

// Function: calculateMeterdBmToPercentage
// Description: Converts 433 MHz RSSI (dBm) to a percentage (0-100%).
int calculateMeterdBmToPercentage(int rssi_dbm) {
  // Clamp RSSI to a reasonable range (e.g., -120 dBm to -40 dBm)
  int clamped_rssi = constrain(rssi_dbm, -120, -40);
  
  // Map the clamped RSSI value to a percentage (0-100%)
  return map(clamped_rssi, -120, -40, 0, 100);
}

// Function: calculateLQIToPercentage
// Description: Converts LQI (Link Quality Indicator) to a percentage (0-100%).
int calculateLQIToPercentage(int lqi) {
  int strength = constrain(lqi, 0, 255); // Clamp LQI to valid range
  return map(strength, 0, 255, 0, 100);  // Map LQI to percentage
}


// Function: onUpdateData
// Description: Fetches data from the water meter and publishes it to MQTT topics.
//              Retries up to 10 times if data retrieval fails.
void onUpdateData()
{
  Serial.printf("Updating data from meter...\n");
  Serial.printf("Retry count : %d\n", _retry);
  Serial.printf("Reading schedule : %s\n", readingSchedule.c_str());

  // Indicate activity with LED
  digitalWrite(LED_BUILTIN, LOW); // Turn on LED to indicate activity

  // Notify MQTT that active reading has started
  mqtt.publish("everblu/cyble/active_reading", "true", true);

  struct tmeter_data meter_data = get_meter_data(); // Fetch meter data

  // Get current UTC time
  time_t tnow = time(nullptr);
  struct tm *ptm = gmtime(&tnow);
  Serial.printf("Current date (UTC) : %04d/%02d/%02d %02d:%02d/%02d - %s\n", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec, String(tnow, DEC).c_str());

  char iso8601[128];
  strftime(iso8601, sizeof iso8601, "%FT%TZ", gmtime(&tnow));

  // Handle data retrieval failure
  if (meter_data.reads_counter == 0 || meter_data.liters == 0) {
    Serial.println("Unable to retrieve data from meter. Retry later...");
    if (_retry++ < 10)
      mqtt.executeDelayed(1000 * 10, onUpdateData); // Retry in 10 seconds
    return;
  }

  
  // Format int time_start and time_end as "HH:MM"
  char timeStartFormatted[6];
  char timeEndFormatted[6];
  int timeStart = constrain(meter_data.time_start, 0, 23); // Ensure valid hour range
  int timeEnd = constrain(meter_data.time_end, 0, 23);     // Ensure valid hour range
  snprintf(timeStartFormatted, sizeof(timeStartFormatted), "%02d:00", timeStart);
  snprintf(timeEndFormatted, sizeof(timeEndFormatted), "%02d:00", timeEnd);

  Serial.printf("Data from meter:\n");
  Serial.printf("Liters : %d\nBattery (in months) : %d\nCounter : %d\nRSSI : %d\nTime start : %s\nTime end : %s\n\n", meter_data.liters, meter_data.battery_left, meter_data.reads_counter, meter_data.rssi, timeStartFormatted, timeEndFormatted);
  Serial.printf("RSSI (dBm) : %d\n", meter_data.rssi_dbm);
  Serial.printf("RSSI (percentage) : %d\n", calculateMeterdBmToPercentage(meter_data.rssi_dbm));
  Serial.printf("Signal quality (LQI) : %d\n", meter_data.lqi);
  Serial.printf("Signal quality (LQI percentage) : %d\n", calculateLQIToPercentage(meter_data.lqi));

  // Publish meter data to MQTT
  mqtt.publish("everblu/cyble/liters", String(meter_data.liters, DEC), true);
  delay(50);
  mqtt.publish("everblu/cyble/counter", String(meter_data.reads_counter, DEC), true);
  delay(50);
  mqtt.publish("everblu/cyble/battery", String(meter_data.battery_left, DEC), true);
  delay(50);
  mqtt.publish("everblu/cyble/rssi_dbm", String(meter_data.rssi_dbm, DEC), true);
  delay(50);
  mqtt.publish("everblu/cyble/rssi_percentage", String(calculateMeterdBmToPercentage(meter_data.rssi_dbm), DEC), true);
  delay(50);
  mqtt.publish("everblu/cyble/lqi", String(meter_data.lqi, DEC), true); // Publish LQI
  delay(50);
  mqtt.publish("everblu/cyble/time_start", timeStartFormatted, true);
  delay(50);
  mqtt.publish("everblu/cyble/time_end", timeEndFormatted, true);
  delay(50);
  mqtt.publish("everblu/cyble/timestamp", iso8601, true); // timestamp since epoch in UTC
  delay(50);
  mqtt.publish("everblu/cyble/lqi_percentage", String(calculateLQIToPercentage(meter_data.lqi), DEC), true);   // Publish LQI percentage to MQTT
  delay(50);

  // Publish all data as a JSON message as well this is redundant but may be useful for some
  char json[512];
  sprintf(json, jsonTemplate, meter_data.liters, meter_data.reads_counter, meter_data.battery_left, meter_data.rssi, iso8601);
  mqtt.publish("everblu/cyble/json", json, true);

  // Notify MQTT that active reading has ended
  mqtt.publish("everblu/cyble/active_reading", "false", true);
  digitalWrite(LED_BUILTIN, HIGH); // Turn off LED to indicate completion

  Serial.printf("Data update complete.\n\n");
}

// Function: onScheduled
// Description: Schedules daily meter readings at 10:00 AM UTC.
void onScheduled()
{
  time_t tnow = time(nullptr);
  struct tm *ptm = gmtime(&tnow);

  // Check if today is a valid reading day
  if (isReadingDay(ptm) && ptm->tm_hour == 10 && ptm->tm_min == 0 && ptm->tm_sec == 0) {
    // Call back in 23 hours
    mqtt.executeDelayed(1000 * 60 * 60 * 23, onScheduled);

    Serial.println("It is time to update data from meter :)");

    // Update data
    _retry = 0;
    onUpdateData();
    return;
  }

  // Check every 500 ms
  mqtt.executeDelayed(500, onScheduled);
}

// Supported abbreviations in MQTT discovery messages for Home Assistant
// Used to reduce the size of the JSON payload
// https://www.home-assistant.io/integrations/mqtt/#supported-abbreviations-in-mqtt-discovery-messages

// JSON Discovery for Reading (Total)
// This is used to show the total water usage in Home Assistant
String jsonDiscoveryReading = R"rawliteral(
{
  "name": "Reading (Total)",
  "uniq_id": "water_meter_value",
  "obj_id": "water_meter_value",
  "ic": "mdi:water",
  "unit_of_meas": "L",
  "device_class": "water",
  "state_class": "total_increasing",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/liters",
  "frc_upd": "true",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Battery Level
// This is used to show the battery level in Home Assistant
String jsonDiscoveryBattery = R"rawliteral(
{
  "name": "Battery",
  "uniq_id": "water_meter_battery",
  "obj_id": "water_meter_battery",
  "device_class": "battery",
  "ic": "mdi:battery",
  "unit_of_meas": "%",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/battery",
  "value_template": "{{ [(value|int), 100] | min }}",
  "frc_upd": "true",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Read Counter
// This is used to show the number of times the meter has been read in Home Assistant
String jsonDiscoveryReadCounter = R"rawliteral(
{
  "name": "Read Counter",
  "uniq_id": "water_meter_counter",
  "obj_id": "water_meter_counter",
  "ic": "mdi:counter",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/counter",
  "frc_upd": "true",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Last Read (timestamp)
// This is used to show the last time the meter was read in Home Assistant
String jsonDiscoveryLastRead = R"rawliteral(
{
  "name": "Last Read",
  "uniq_id": "water_meter_timestamp",
  "obj_id": "water_meter_timestamp",
  "device_class": "timestamp",
  "ic": "mdi:clock",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/timestamp",
  "frc_upd": "true",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Request Reading (button)
// This is used to trigger a reading from the meter when pressed in Home Assistant
String jsonDiscoveryRequestReading = R"rawliteral(
{
  "name": "Request Reading Now",
  "uniq_id": "water_meter_request",
  "obj_id": "water_meter_request",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "cmd_t": "everblu/cyble/trigger",
  "payload_available": "online",
  "payload_not_available": "offline",
  "pl_prs": "update",
  "frc_upd": "true",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Active Reading (binary sensor)
// This is used to indicate that the device is currently reading data from the meter
String jsonDiscoveryActiveReading = R"rawliteral(
{
  "name": "Active Reading",
  "uniq_id": "water_meter_active_reading",
  "obj_id": "water_meter_active_reading",
  "device_class": "running",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/active_reading",
  "payload_on": "true",
  "payload_off": "false",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Wi-Fi Details
// These are used to provide information about the Wi-Fi connection of the device
String jsonDiscoveryWifiIP = R"rawliteral(
{
  "name": "IP Address",
  "uniq_id": "water_meter_wifi_ip",
  "obj_id": "water_meter_wifi_ip",
  "ic": "mdi:ip-network-outline",
  "qos": 0,
  "stat_t": "everblu/cyble/wifi_ip",
  "frc_upd": "true",
  "ent_cat": "diagnostic",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Wi-Fi RSSI
// This is used to show the Wi-Fi signal strength in Home Assistant
String jsonDiscoveryWifiRSSI = R"rawliteral(
{
  "name": "WiFi RSSI",
  "uniq_id": "water_meter_wifi_rssi",
  "obj_id": "water_meter_wifi_rssi",
  "device_class": "signal_strength",
  "ic": "mdi:signal-variant",
  "unit_of_meas": "dBm",
  "qos": 0,
  "stat_t": "everblu/cyble/wifi_rssi",
  "frc_upd": "true",
  "ent_cat": "diagnostic",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Wi-Fi Signal Percentage
// This is used to show the Wi-Fi signal strength as a percentage in Home Assistant
String jsonDiscoveryWifiSignalPercentage = R"rawliteral(
{
  "name": "WiFi Signal",
  "uniq_id": "water_meter_wifi_signal_percentage",
  "obj_id": "water_meter_wifi_signal_percentage",
  "ic": "mdi:wifi",
  "unit_of_meas": "%",
  "qos": 0,
  "stat_t": "everblu/cyble/wifi_signal_percentage",
  "frc_upd": "true",
  "ent_cat": "diagnostic",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for MAC Address
// This is used to show the MAC address of the device in Home Assistant
String jsonDiscoveryMacAddress = R"rawliteral(
{
  "name": "MAC Address",
  "uniq_id": "water_meter_mac_address",
  "obj_id": "water_meter_mac_address",
  "ic": "mdi:network",
  "qos": 0,
  "stat_t": "everblu/cyble/mac_address",
  "frc_upd": "true",
  "ent_cat": "diagnostic",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for BSSID
// This is used to show the BSSID of the device in Home Assistant
String jsonDiscoveryBSSID = R"rawliteral(
{
  "name": "WiFi BSSID",
  "uniq_id": "water_meter_wifi_bssid",
  "obj_id": "water_meter_wifi_bssid",
  "ic": "mdi:access-point-network",
  "qos": 0,
  "stat_t": "everblu/cyble/bssid",
  "frc_upd": "true",
  "ent_cat": "diagnostic",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Wi-Fi SSID
// This is used to show the SSID of the device in Home Assistant
String jsonDiscoverySSID = R"rawliteral(
{
  "name": "WiFi SSID",
  "uniq_id": "water_meter_wifi_ssid",
  "obj_id": "water_meter_wifi_ssid",
  "ic": "mdi:help-network-outline",
  "qos": 0,
  "stat_t": "everblu/cyble/ssid",
  "frc_upd": "true",
  "ent_cat": "diagnostic",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Uptime
// This is used to show the uptime of the device in Home Assistant
String jsonDiscoveryUptime = R"rawliteral(
{
  "name": "Device Uptime",
  "uniq_id": "water_meter_uptime",
  "obj_id": "water_meter_uptime",
  "device_class": "timestamp",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/uptime",
  "frc_upd": "true",
  "ent_cat": "diagnostic",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Restart Button
// This is used to trigger a restart of the device when pressed in Home Assistant
String jsonDiscoveryRestartButton = R"rawliteral(
{
  "name": "Restart Device",
  "uniq_id": "water_meter_restart",
  "obj_id": "water_meter_restart",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "cmd_t": "everblu/cyble/restart",
  "pl_prs": "restart",
  "ent_cat": "config",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Meter Year
String jsonDiscoveryMeterYear = R"rawliteral(
{
  "name": "Meter Year",
  "uniq_id": "water_meter_year",
  "obj_id": "water_meter_year",
  "ic": "mdi:calendar",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/water_meter_year",
  "frc_upd": "true",
  "ent_cat": "diagnostic",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Meter Serial
String jsonDiscoveryMeterSerial = R"rawliteral(
{
  "name": "Meter Serial",
  "uniq_id": "water_meter_serial",
  "obj_id": "water_meter_serial",
  "ic": "mdi:barcode",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/water_meter_serial",
  "frc_upd": "true",
  "ent_cat": "diagnostic",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Frequency
String jsonDiscoveryFrequency = R"rawliteral(
{
  "name": "Meter Frequency",
  "uniq_id": "water_meter_frequency",
  "obj_id": "water_meter_frequency",
  "ic": "mdi:signal",
  "unit_of_meas": "MHz",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/water_meter_frequency",
  "frc_upd": "true",
  "ent_cat": "diagnostic",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Reading Schedule
String jsonDiscoveryReadingSchedule = R"rawliteral(
{
  "name": "Reading Schedule",
  "uniq_id": "water_meter_reading_schedule",
  "obj_id": "water_meter_reading_schedule",
  "ic": "mdi:calendar-clock",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/reading_schedule",
  "frc_upd": "true",
  "ent_cat": "diagnostic",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Battery Months Left
String jsonDiscoveryBatteryMonths = R"rawliteral(
{
  "name": "Months Remaining",
  "uniq_id": "water_meter_battery_months",
  "obj_id": "water_meter_battery_months",
  "device_class": "duration",
  "ic": "mdi:battery-clock",
  "unit_of_meas": "months",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/battery",
  "frc_upd": "true",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Meter RSSI (dBm)
String jsonDiscoveryMeterRSSIDBm = R"rawliteral(
{
  "name": "RSSI",
  "uniq_id": "water_meter_rssi_dbm",
  "obj_id": "water_meter_rssi_dbm",
  "device_class": "signal_strength",
  "ic": "mdi:signal",
  "unit_of_meas": "dBm",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/rssi_dbm",
  "frc_upd": "true",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Meter RSSI (Percentage)
String jsonDiscoveryMeterRSSIPercentage = R"rawliteral(
{
  "name": "Signal",
  "uniq_id": "water_meter_rssi_percentage",
  "obj_id": "water_meter_rssi_percentage",
  "ic": "mdi:signal-cellular-3",
  "unit_of_meas": "%",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/rssi_percentage",
  "frc_upd": "true",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Meter LQI (Link Quality Indicator)
String jsonDiscoveryLQIPercentage = R"rawliteral(
  {
    "name": "Signal Quality (LQI)",
    "uniq_id": "water_meter_lqi_percentage",
    "obj_id": "water_meter_lqi_percentage",
    "ic": "mdi:signal-cellular-outline",
    "unit_of_meas": "%",
    "qos": 0,
    "avty_t": "everblu/cyble/status",
    "stat_t": "everblu/cyble/lqi_percentage",
    "frc_upd": "true",
    "dev": {
      "ids": ["14071984"],
      "name": "Water Meter",
      "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
      "mf": "Psykokwak [Forked by Genestealer]"
    }
  }
  )rawliteral";
  
// JSON Discovery for Meter Wake Time
String jsonDiscoveryTimeStart = R"rawliteral(
{
  "name": "Wake Time",
  "uniq_id": "water_meter_time_start",
  "obj_id": "water_meter_time_start",
  "ic": "mdi:clock-start",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/time_start",
  "frc_upd": "true",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";

// JSON Discovery for Meter Sleep Time
String jsonDiscoveryTimeEnd = R"rawliteral(
{
  "name": "Sleep Time",
  "uniq_id": "water_meter_time_end",
  "obj_id": "water_meter_time_end",
  "ic": "mdi:clock-end",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/time_end",
  "frc_upd": "true",
  "dev": {
    "ids": ["14071984"],
    "name": "Water Meter",
    "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
    "mf": "Psykokwak [Forked by Genestealer]"
  }
}
)rawliteral";




// Function: publishWifiDetails
// Description: Publishes Wi-Fi diagnostics (IP, RSSI, signal strength, etc.) to MQTT.
void publishWifiDetails() {
  Serial.println("> Publish Wi-Fi details");
  String wifiIP = WiFi.localIP().toString();
  int wifiRSSI = WiFi.RSSI();
  int wifiSignalPercentage = calculateWiFiSignalStrengthPercentage(wifiRSSI); // Convert RSSI to percentage
  String macAddress = WiFi.macAddress();
  String wifiSSID = WiFi.SSID();
  String wifiBSSID = WiFi.BSSIDstr();
  String status = (WiFi.status() == WL_CONNECTED) ? "online" : "offline";

  // Uptime calculation
  unsigned long uptimeMillis = millis();
  time_t uptimeSeconds = uptimeMillis / 1000;
  time_t now = time(nullptr);
  time_t uptimeTimestamp = now - uptimeSeconds;
  char uptimeISO[32];
  strftime(uptimeISO, sizeof(uptimeISO), "%FT%TZ", gmtime(&uptimeTimestamp));

  // Publish diagnostic sensors
  mqtt.publish("everblu/cyble/wifi_ip", wifiIP, true);
  delay(50);
  mqtt.publish("everblu/cyble/wifi_rssi", String(wifiRSSI, DEC), true);
  delay(50);
  mqtt.publish("everblu/cyble/wifi_signal_percentage", String(wifiSignalPercentage, DEC), true);
  delay(50);
  mqtt.publish("everblu/cyble/mac_address", macAddress, true);
  delay(50);
  mqtt.publish("everblu/cyble/ssid", wifiSSID, true);
  delay(50);
  mqtt.publish("everblu/cyble/bssid", wifiBSSID, true);
  delay(50);
  mqtt.publish("everblu/cyble/status", status, true);
  delay(50);
  mqtt.publish("everblu/cyble/uptime", uptimeISO, true);
  delay(50);

  Serial.println("> Wi-Fi details published");
}

// Function: publishMeterSettings
// Description: Publishes meter configuration (year, serial, frequency) to MQTT.
void publishMeterSettings() {
  Serial.println("> Publish meter settings");

  // Publish Meter Year, Serial, and Frequency
  mqtt.publish("everblu/cyble/water_meter_year", String(METER_YEAR, DEC), true);
  delay(50);
  mqtt.publish("everblu/cyble/water_meter_serial", String(METER_SERIAL, DEC), true);
  delay(50);
  mqtt.publish("everblu/cyble/water_meter_frequency", String(FREQUENCY, 6), true);
  delay(50);

  // Publish Reading Schedule
  mqtt.publish("everblu/cyble/reading_schedule", readingSchedule, true);
  delay(50);

  Serial.println("> Meter settings published");
}

// Function: onConnectionEstablished
// Description: Handles MQTT connection establishment, including Home Assistant discovery and OTA setup.
void onConnectionEstablished()
{
  Serial.println("Connected to MQTT Broker :)");

  Serial.println("> Configure time from NTP server. Please wait...");
  // Note, my VLAN has no WAN/internet, so I am useing Home Assistant Community Add-on: chrony to proxy the time
  configTzTime("UTC0", secret_local_timeclock_server);

  delay(5000); // Give it a moment for the time to sync the print out the time
  time_t tnow = time(nullptr);
  struct tm *ptm = gmtime(&tnow);
  Serial.printf("Current date (UTC) : %04d/%02d/%02d %02d:%02d/%02d - %s\n", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec, String(tnow, DEC).c_str());
  
  Serial.println("> Configure Arduino OTA flash.");
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    }
    else { // U_FS
      type = "filesystem";
    }
    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd updating.");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("%u%%\r\n", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    }
    else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    }
    else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    }
    else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    }
    else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.setHostname("EVERBLUREADER");
  ArduinoOTA.begin();
  Serial.println("> Ready");
  Serial.print("> IP address: ");
  Serial.println(WiFi.localIP());

  mqtt.subscribe("everblu/cyble/trigger", [](const String& message) {
    if (message.length() > 0) {

      Serial.println("Update data from meter from MQTT trigger");

      _retry = 0;
      onUpdateData();
    }
  });

  mqtt.subscribe("everblu/cyble/restart", [](const String& message) {
    if (message == "restart") {
      Serial.println("Restart command received via MQTT. Restarting...");
      ESP.restart(); // Restart the ESP device
    }
  });

  Serial.println("> Send MQTT config for HA.");

  // Publish Meter details discovery configuration
  delay(50);
  mqtt.publish("homeassistant/sensor/water_meter_value/config", jsonDiscoveryReading, true);
  delay(50);
  mqtt.publish("homeassistant/sensor/water_meter_battery/config", jsonDiscoveryBattery, true);
  delay(50);
  mqtt.publish("homeassistant/sensor/water_meter_counter/config", jsonDiscoveryReadCounter, true);
  delay(50);
  mqtt.publish("homeassistant/sensor/water_meter_timestamp/config", jsonDiscoveryLastRead, true);
  delay(50);
  mqtt.publish("homeassistant/button/water_meter_request/config", jsonDiscoveryRequestReading, true);
  delay(50);

  // Publish Wi-Fi details discovery configuration
  mqtt.publish("homeassistant/sensor/water_meter_wifi_ip/config", jsonDiscoveryWifiIP, true);
  delay(50);
  mqtt.publish("homeassistant/sensor/water_meter_wifi_rssi/config", jsonDiscoveryWifiRSSI, true);
  delay(50);
  mqtt.publish("homeassistant/sensor/water_meter_mac_address/config", jsonDiscoveryMacAddress, true);
  delay(50);
  mqtt.publish("homeassistant/sensor/water_meter_wifi_ssid/config", jsonDiscoverySSID, true);
  delay(50);
  mqtt.publish("homeassistant/sensor/water_meter_wifi_bssid/config", jsonDiscoveryBSSID, true);
  delay(50);
  mqtt.publish("homeassistant/sensor/water_meter_uptime/config", jsonDiscoveryUptime, true);
  delay(50);
  mqtt.publish("homeassistant/sensor/water_meter_wifi_signal_percentage/config", jsonDiscoveryWifiSignalPercentage, true);
  delay(50);

  // Publish MQTT discovery messages for the Restart Button
  mqtt.publish("homeassistant/button/water_meter_restart/config", jsonDiscoveryRestartButton, true);
  delay(50);

  // Publish MQTT discovery message for the binary sensor
  mqtt.publish("homeassistant/binary_sensor/water_meter_active_reading/config", jsonDiscoveryActiveReading, true);
  delay(50);

  // Publish MQTT discovery messages for Meter Year, Serial, and Frequency
  mqtt.publish("homeassistant/sensor/water_meter_year/config", jsonDiscoveryMeterYear, true);
  delay(50);
  mqtt.publish("homeassistant/sensor/water_meter_serial/config", jsonDiscoveryMeterSerial, true);
  delay(50);
  mqtt.publish("homeassistant/sensor/water_meter_frequency/config", jsonDiscoveryFrequency, true);
  delay(50);

  // Publish JSON discovery for Reading Schedule
  mqtt.publish("homeassistant/sensor/water_meter_reading_schedule/config", jsonDiscoveryReadingSchedule, true);
  delay(50);

  // Publish JSON discovery for Battery Months Left
  mqtt.publish("homeassistant/sensor/water_meter_battery_months/config", jsonDiscoveryBatteryMonths, true);
  delay(50);

  // Publish JSON discovery for Meter RSSI (dBm), RSSI (%), and LQI (%)
  mqtt.publish("homeassistant/sensor/water_meter_rssi_dbm/config", jsonDiscoveryMeterRSSIDBm, true);
  delay(50);
  mqtt.publish("homeassistant/sensor/water_meter_rssi_percentage/config", jsonDiscoveryMeterRSSIPercentage, true);
  delay(50);
  mqtt.publish("homeassistant/sensor/water_meter_lqi_percentage/config", jsonDiscoveryLQIPercentage, true);
  delay(50);

  // Publish JSON discovery for the times the meter wakes and sleeps
  mqtt.publish("homeassistant/sensor/water_meter_time_start/config", jsonDiscoveryTimeStart, true);
  delay(50);
  mqtt.publish("homeassistant/sensor/water_meter_time_end/config", jsonDiscoveryTimeEnd, true);
  delay(50);

  // Set initial state for active reading
  mqtt.publish("everblu/cyble/active_reading", "false", true);
  delay(50);

  Serial.println("> MQTT config sent");

  // Publish initial Wi-Fi details
  publishWifiDetails();

  // Publish once the meter settings as set in the softeware
  publishMeterSettings();

  // Turn off LED to show everything is setup
  digitalWrite(LED_BUILTIN, HIGH); // turned off

  Serial.println("> Setup done");
  Serial.println("Ready to go...");

  onScheduled();
}

// Function: setup
// Description: Initializes the device, including serial communication, Wi-Fi, MQTT, and frequency discovery.
void setup()
{
  Serial.begin(115200);
  Serial.println("\n");
  Serial.println("Everblu Meters ESP8266 Starting...");
  Serial.println("Water usage data for Home Assistant");
  Serial.println("https://github.com/genestealer/everblu-meters-esp8266-improved");
  String meterinfo = "Target meter: " + String(METER_YEAR, DEC) + "-0" + String(METER_SERIAL, DEC) + "\nTarget frequency: " + String(FREQUENCY, DEC) + "\n";
  Serial.println(meterinfo);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW); // turned on to start with

  // Increase the max packet size to handle large MQTT payloads
  mqtt.setMaxPacketSize(2048); // Set to a size larger than your longest payload

  // Set the Last Will and Testament (LWT)
  mqtt.enableLastWillMessage("everblu/cyble/status", "offline", true);  // You can activate the retain flag by setting the third parameter to true

  // Conditionally enable Wi-Fi PHY mode 11G.  Set this in private.h to 0 to disable.
  #if ENABLE_WIFI_PHY_MODE_11G
  WiFi.setPhyMode(WIFI_PHY_MODE_11G);
  Serial.println("Wi-Fi PHY mode set to 11G.");
  #else
  Serial.println("> Wi-Fi PHY mode 11G is disabled.");
  #endif

  // Log the reading schedule
  Serial.printf("> Reading schedule: %s\n", DEFAULT_READING_SCHEDULE);

  // Optional functionalities of EspMQTTClient
  // mqtt.enableDebuggingMessages(true); // Enable debugging messages sent to serial output

  // Conditionally enable scan for meter on 433 MHz. Set this in private.h to 1 to enable.  Only needed to find the frequency of the meter once.
  #if SCAN_FREQUENCY_433MHZ
  scanFrequency433MHz();
  #endif

    
  // Set CC1101 radio frequency
  cc1101_init(FREQUENCY);

  /*
  // Use this piece of code to test
  struct tmeter_data meter_data;
  meter_data = get_meter_data();
  Serial.printf("\nLiters : %d\nBattery (in months) : %d\nCounter : %d\nTime start : %d\nTime end : %d\n\n", meter_data.liters, meter_data.battery_left, meter_data.reads_counter, meter_data.time_start, meter_data.time_end);
  while (42);
  */

}

// Function: loop
// Description: Main loop to handle MQTT and OTA operations, and update diagnostics periodically.
void loop() {
  mqtt.loop();
  ArduinoOTA.handle();

  // Update diagnostics and Wi-Fi details every 5 minutes
  if (millis() - lastWifiUpdate > 300000) { // 5 minutes in ms
    publishWifiDetails();
    lastWifiUpdate = millis();
  }
}
