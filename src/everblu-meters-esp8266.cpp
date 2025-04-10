// Project source : 
// http://www.lamaisonsimon.fr/wiki/doku.php?id=maison2:compteur_d_eau:compteur_d_eau

// Note: Libraries are included in "Project Dependencies" file platformio.ini
// Note: Cases are important in the include statements. Make sure to use the correct case for the library names.
#include "private.h"        // Include the local private file for passwords etc. not for GitHub. Generate your own private.h file with the same content as private_example.h
#include "everblu_meters.h" // Include the local everblu_meters library
#include <ESP8266WiFi.h>    // Include the ESP8266 Wi-Fi library
#include <ESP8266mDNS.h>    // Include the ESP8266 mDNS library
#include <Arduino.h>        // Include the Arduino library
#include <ArduinoOTA.h>     // Include the Arduino OTA library
#include <EspMQTTClient.h>  // Include the EspMQTTClient library

#ifndef LED_BUILTIN
// Change this pin if needed
#define LED_BUILTIN 2
#endif

unsigned long lastWifiUpdate = 0;

EspMQTTClient mqtt(
    secret_wifi_ssid,     // Your Wifi SSID
    secret_wifi_password, // Your WiFi key
    secret_mqtt_server,   // MQTT Broker server ip
    secret_mqtt_username, // MQTT Username Can be omitted if not needed
    secret_mqtt_password, // MQTT Password Can be omitted if not needed
    secret_clientName,    // MQTT Client name that uniquely identify your device
    1883                  // MQTT Broker server port
);

// char *jsonTemplate = "{\"liters\": %d, \"counter\" : %d, \"battery\" : %d, \"timestamp\" : \"%s\" }";

// const char jsonTemplate[] = "{ \"liters\": %d, \"counter\" : %d, \"battery\" : %d, \"timestamp\" : \"%s\" }";

const char jsonTemplate[] = "{ "
                            "\"liters\": %d, "
                            "\"counter\" : %d, "
                            "\"battery\" : %d, "
                            "\"timestamp\" : \"%s\" }";

int _retry = 0;
void onUpdateData()
{
  // Set LED to indicate activity
  digitalWrite(LED_BUILTIN, LOW); // Turn on LED to show activity

  // Publish active reading state as true
  mqtt.publish("everblu/cyble/active_reading", "true", true);

  struct tmeter_data meter_data;
  meter_data = get_meter_data();

  time_t tnow = time(nullptr);
  struct tm *ptm = gmtime(&tnow);
  Serial.printf("Current date (UTC) : %04d/%02d/%02d %02d:%02d:%02d - %s\n", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec, String(tnow, DEC).c_str());

  char iso8601[128];
  strftime(iso8601, sizeof iso8601, "%FT%TZ", gmtime(&tnow));

  if (meter_data.reads_counter == 0 || meter_data.liters == 0) {
    Serial.println("Unable to retrieve data from meter. Retry later...");

    // Call back this function in 10 sec (in miliseconds)
    if (_retry++ < 10)
      mqtt.executeDelayed(1000 * 10, onUpdateData);
    return;
  }

  Serial.printf("Liters : %d\nBattery (in months) : %d\nCounter : %d\n\n", meter_data.liters, meter_data.battery_left, meter_data.reads_counter);

  mqtt.publish("everblu/cyble/liters", String(meter_data.liters, DEC), true);
  delay(50);
  mqtt.publish("everblu/cyble/counter", String(meter_data.reads_counter, DEC), true);
  delay(50);
  mqtt.publish("everblu/cyble/battery", String(meter_data.battery_left, DEC), true);
  delay(50);
  mqtt.publish("everblu/cyble/timestamp", iso8601, true); // timestamp since epoch in UTC
  delay(50);

  char json[512];
  sprintf(json, jsonTemplate, meter_data.liters, meter_data.reads_counter, meter_data.battery_left, iso8601);
  mqtt.publish("everblu/cyble/json", json, true); // send all data as a json message

  // Turn off active reading state
  mqtt.publish("everblu/cyble/active_reading", "false", true);
  digitalWrite(LED_BUILTIN, HIGH); // Turn off LED now that data has been pulled
}

// This function calls onUpdateData() every days at 10:00am UTC
void onScheduled()
{
  time_t tnow = time(nullptr);
  struct tm *ptm = gmtime(&tnow);

  // At 10:00:00am UTC
  if (ptm->tm_hour == 10 && ptm->tm_min == 0 && ptm->tm_sec == 0) {

    // Call back in 23 hours
    mqtt.executeDelayed(1000 * 60 * 60 * 23, onScheduled);

    Serial.println("It is time to update data from meter :)");

    // Update data
    _retry = 0;
    onUpdateData();

    return;
  }

  // Every 500 ms
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
  "avty_t": "everblu/cyble/status",
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
  "avty_t": "everblu/cyble/status",
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
  "avty_t": "everblu/cyble/status",
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
  "avty_t": "everblu/cyble/status",
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
  "avty_t": "everblu/cyble/status",
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
  "avty_t": "everblu/cyble/status",
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

int calculateWiFiSignalStrengthPercentage(int rssi) {
  int strength = constrain(rssi, -100, -50); // Clamp RSSI to a reasonable range
  return map(strength, -100, -50, 0, 100);   // Map RSSI to percentage (0-100%)
}

void publishWifiDetails() {
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
}

void publishMeterSettings() {
  // Publish Meter Year, Serial, and Frequency
  mqtt.publish("everblu/cyble/water_meter_year", String(METER_YEAR, DEC), true);
  delay(50);
  mqtt.publish("everblu/cyble/water_meter_serial", String(METER_SERIAL, DEC), true);
  delay(50);
  mqtt.publish("everblu/cyble/water_meter_frequency", String(FREQUENCY, 6), true);
  delay(50);
}

void onConnectionEstablished()
{
  Serial.println("Connected to MQTT Broker :)");

  Serial.println("> Configure time from NTP server. Please wait...");
  // Note, my VLAN has no WAN/internet, so I am useing Home Assistant Community Add-on: chrony to proxy the time
  configTzTime("UTC0", secret_local_timeclock_server);

  delay(5000); // Give it a moment for the time to sync the print out the time
  time_t tnow = time(nullptr);
  struct tm *ptm = gmtime(&tnow);
  Serial.printf("Current date (UTC) : %04d/%02d/%02d %02d:%02d:%02d - %s\n", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec, String(tnow, DEC).c_str());
  
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
  Serial.println("Ready");
  Serial.print("IP address: ");
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
  // Auto discovery
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

  // Set initial state for active reading
  mqtt.publish("everblu/cyble/active_reading", "false", true);
  delay(50);

  // Publish initial Wi-Fi details
  publishWifiDetails();

  // Publish once the meter settings as set in the softeware
  publishMeterSettings();

  // Turn off LED to show everything is setup
  digitalWrite(LED_BUILTIN, HIGH); // turned off

  onScheduled();
}

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

  // Optional functionalities of EspMQTTClient
  // mqtt.enableDebuggingMessages(true); // Enable debugging messages sent to serial output

  // Frequency Discovery
  // Use this piece of code to find the right frequency to use going forwards. Un-comment for first use. Re-comment once you have your meter's values.
  // Note: Some meters are configured to broadcast their data only at specific times, typically this is during nominal working hours, so try to do this process within those times
  /*
  Serial.printf("###### FREQUENCY DISCOVERY ENABLED ######\nStarting Frequency Scan...\n");
  for (float i = 433.76f; i < 433.890f; i += 0.0005f) {
    Serial.printf("Test frequency : %f\n", i);
    cc1101_init(i);
    struct tmeter_data meter_data;
    meter_data = get_meter_data();
    if (meter_data.reads_counter != 0 || meter_data.liters != 0) {
      Serial.printf("\n------------------------------\nGot frequency : %f\n------------------------------\n", i);
      Serial.printf("Liters : %d\nBattery (in months) : %d\nCounter : %d\n\n", meter_data.liters, meter_data.battery_left, meter_data.reads_counter);
      digitalWrite(LED_BUILTIN, LOW); // turned on
      while (42);
    }
  }
    Serial.printf("###### FREQUENCY DISCOVERY FINISHED ######\nOnce you have discovered the correct frequency you can disable this scan.\n\n");
  */

  cc1101_init(FREQUENCY);

  /*
  // Use this piece of code to test
  struct tmeter_data meter_data;
  meter_data = get_meter_data();
  Serial.printf("\nLiters : %d\nBattery (in months) : %d\nCounter : %d\nTime start : %d\nTime end : %d\n\n", meter_data.liters, meter_data.battery_left, meter_data.reads_counter, meter_data.time_start, meter_data.time_end);
  while (42);
  */

}

void loop() {
  mqtt.loop();
  ArduinoOTA.handle();

  // Update diagnostics and Wi-Fi details every 5 minutes
  if (millis() - lastWifiUpdate > 300000) { // 5 minutes in ms
    publishWifiDetails();
    lastWifiUpdate = millis();
  }
}