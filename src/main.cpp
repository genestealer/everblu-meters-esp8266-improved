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
 * - Automatic frequency synthesizer calibration and offset compensation
 * - Wi-Fi diagnostics and OTA updates
 * - Daily scheduled meter readings
 * 
 * For more details, refer to the README file.
 */

#include "private.h"         // Include private configuration (Wi-Fi, MQTT, etc.)
#include "everblu_meters.h" // Include EverBlu meter communication library
#if defined(ESP8266)
#include <ESP8266WiFi.h>    // Wi-Fi library for ESP8266
#include <ESP8266mDNS.h>    // mDNS library for ESP8266
#include <EEPROM.h>         // EEPROM library for ESP8266
#elif defined(ESP32)
#include <WiFi.h>           // Wi-Fi library for ESP32
#include <ESPmDNS.h>        // mDNS library for ESP32
#include <Preferences.h>    // Preferences library for ESP32
#include <esp_task_wdt.h>   // Watchdog timer for ESP32
#endif
#include <Arduino.h>        // Core Arduino library
#include <ArduinoOTA.h>     // OTA update library
#include <EspMQTTClient.h>  // MQTT client library
#include <math.h>           // For floor/ceil during scan alignment

// Cross-platform watchdog feed helper
static inline void FEED_WDT() {
#if defined(ESP8266)
  ESP.wdtFeed();
#elif defined(ESP32)
  esp_task_wdt_reset();
  yield();
#else
  (void)0;
#endif
}

// Define the LED_BUILTIN pin if missing
#ifndef LED_BUILTIN
#define LED_BUILTIN 2 // Change this pin if needed
#endif

// Define the Wi-Fi PHY mode if missing from the private.h file
#ifndef ENABLE_WIFI_PHY_MODE_11G
#define ENABLE_WIFI_PHY_MODE_11G 0  // Set to 1 to enable 11G PHY mode
#endif

// Define the default reading schedule if missing from the private.h file.
// Options: "Monday-Friday", "Monday-Saturday", or "Monday-Sunday"
#ifndef DEFAULT_READING_SCHEDULE
#define DEFAULT_READING_SCHEDULE "Monday-Friday"
#endif

// Optional: configurable daily read time in UTC (hour/minute)
// If not defined in private.h, defaults to 10:00 UTC
#ifndef DEFAULT_READING_HOUR_UTC
#define DEFAULT_READING_HOUR_UTC 10
#endif
#ifndef DEFAULT_READING_MINUTE_UTC
#define DEFAULT_READING_MINUTE_UTC 0
#endif

// Auto-align scheduled reading time to the meter's wake window (time_start/time_end)
// 1 = enabled (default), 0 = disabled
#ifndef AUTO_ALIGN_READING_TIME
#define AUTO_ALIGN_READING_TIME 1
#endif

// Alignment strategy: 0 = use time_start, 1 = use midpoint of [time_start, time_end]
#ifndef AUTO_ALIGN_USE_MIDPOINT
#define AUTO_ALIGN_USE_MIDPOINT 1
#endif

// Resolved reading time (UTC) which may be updated dynamically after a successful read
static int g_readHourUtc = DEFAULT_READING_HOUR_UTC;
static int g_readMinuteUtc = DEFAULT_READING_MINUTE_UTC;

// Define a default meter frequency if missing from private.h.
// RADIAN protocol nominal center frequency for EverBlu is 433.82 MHz.
#ifndef FREQUENCY
#define FREQUENCY 433.82
#define FREQUENCY_DEFINED_DEFAULT 1
#else
#define FREQUENCY_DEFINED_DEFAULT 0
#endif

unsigned long lastWifiUpdate = 0;

// Read success/failure metrics
unsigned long totalReadAttempts = 0;
unsigned long successfulReads = 0;
unsigned long failedReads = 0;
const char* lastErrorMessage = "None";

// Frequency offset storage
#define EEPROM_SIZE 64
#define FREQ_OFFSET_ADDR 0
#define FREQ_OFFSET_MAGIC 0xABCD // Magic number to verify valid data
float storedFrequencyOffset = 0.0;
bool autoScanEnabled = true;       // Enable automatic scan on first boot if no offset found
int successfulReadsBeforeAdapt = 0; // Track successful reads for adaptive tuning
float cumulativeFreqError = 0.0;   // Accumulate FREQEST readings for adaptive adjustment
const int ADAPT_THRESHOLD = 10;    // Adapt frequency after N successful reads

#if defined(ESP32)
Preferences preferences;
#endif

// Function prototypes for frequency management
void saveFrequencyOffset(float offset);
float loadFrequencyOffset();
void performFrequencyScan();
void performWideInitialScan();
void adaptiveFrequencyTracking(int8_t freqest);

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
const int MAX_RETRIES = 3; // Maximum number of retry attempts
unsigned long lastFailedAttempt = 0; // Timestamp of last failed attempt
const unsigned long RETRY_COOLDOWN = 3600000; // 1 hour cooldown in milliseconds

// Global variable to store the reading schedule (default from private.h)
const char* readingSchedule = DEFAULT_READING_SCHEDULE;

// Helper: validate schedule string against supported options
static bool isValidReadingSchedule(const char* s) {
  return (strcmp(s, "Monday-Friday") == 0 || 
          strcmp(s, "Monday-Saturday") == 0 || 
          strcmp(s, "Monday-Sunday") == 0);
}

// Ensure schedule is valid; if not, fall back to a safe default and warn
static void validateReadingSchedule() {
  if (!isValidReadingSchedule(readingSchedule)) {
    Serial.printf("WARNING: Invalid reading schedule '%s'. Falling back to 'Monday-Friday'.\n", readingSchedule);
    readingSchedule = "Monday-Friday";
  }
}

// Function to check if today is within the configured schedule
bool isReadingDay(struct tm *ptm) {
  if (strcmp(readingSchedule, "Monday-Friday") == 0) {
    return ptm->tm_wday >= 1 && ptm->tm_wday <= 5; // Monday to Friday
  } else if (strcmp(readingSchedule, "Monday-Saturday") == 0) {
    return ptm->tm_wday >= 1 && ptm->tm_wday <= 6; // Monday to Saturday
  } else if (strcmp(readingSchedule, "Monday-Sunday") == 0) {
    return true; // Every day
  }
  return false;
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
  Serial.printf("Reading schedule : %s\n", readingSchedule);

  // Increment total attempts counter
  totalReadAttempts++;

  // Indicate activity with LED
  digitalWrite(LED_BUILTIN, LOW); // Turn on LED to indicate activity

  // Notify MQTT that active reading has started
  mqtt.publish("everblu/cyble/active_reading", "true", true);
  mqtt.publish("everblu/cyble/cc1101_state", "Reading", true);

  struct tmeter_data meter_data = get_meter_data(); // Fetch meter data

  // Get current UTC time
  time_t tnow = time(nullptr);
  struct tm *ptm = gmtime(&tnow);
  Serial.printf("Current date (UTC) : %04d/%02d/%02d %02d:%02d/%02d - %ld\n", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec, (long)tnow);

  char iso8601[128];
  strftime(iso8601, sizeof iso8601, "%FT%TZ", gmtime(&tnow));

  // Handle data retrieval failure
  if (meter_data.reads_counter == 0 || meter_data.liters == 0) {
    Serial.printf("Unable to retrieve data from meter (attempt %d/%d)\n", _retry + 1, MAX_RETRIES);
    
    if (_retry < MAX_RETRIES - 1) {
      // Schedule retry using callback instead of recursion to prevent stack overflow
      _retry++;
      static char errorMsg[64];
      snprintf(errorMsg, sizeof(errorMsg), "Retry %d/%d - No data received", _retry, MAX_RETRIES);
      lastErrorMessage = errorMsg;
      Serial.printf("Scheduling retry in 10 seconds... (next attempt %d/%d)\n", _retry + 1, MAX_RETRIES);
      mqtt.publish("everblu/cyble/active_reading", "false", true);
      mqtt.publish("everblu/cyble/cc1101_state", "Idle", true);
      mqtt.publish("everblu/cyble/last_error", lastErrorMessage, true);
      digitalWrite(LED_BUILTIN, HIGH); // Turn off LED
      // Use non-blocking callback instead of recursive call
      mqtt.executeDelayed(10000, onUpdateData);
    } else {
      // Max retries reached, enter cooldown period
      lastFailedAttempt = millis();
      failedReads++;
      lastErrorMessage = "Max retries reached - cooling down";
      Serial.printf("Max retries (%d) reached. Entering 1-hour cooldown period.\n", MAX_RETRIES);
      mqtt.publish("everblu/cyble/active_reading", "false", true);
      mqtt.publish("everblu/cyble/cc1101_state", "Idle", true);
      mqtt.publish("everblu/cyble/status_message", "Failed after max retries, cooling down for 1 hour", true);
      mqtt.publish("everblu/cyble/last_error", lastErrorMessage, true);
      
      char buffer[16];
      snprintf(buffer, sizeof(buffer), "%lu", failedReads);
      mqtt.publish("everblu/cyble/failed_reads", buffer, true);
      
      snprintf(buffer, sizeof(buffer), "%lu", totalReadAttempts);
      mqtt.publish("everblu/cyble/total_attempts", buffer, true);
      digitalWrite(LED_BUILTIN, HIGH); // Turn off LED
      _retry = 0; // Reset retry counter for next scheduled attempt
    }
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

  // Publish meter data to MQTT (using char buffers instead of String)
  char valueBuffer[32];
  
  snprintf(valueBuffer, sizeof(valueBuffer), "%d", meter_data.liters);
  mqtt.publish("everblu/cyble/liters", valueBuffer, true);
  delay(5);
  
  // Publish historical data as JSON attributes for Home Assistant
  // This provides 13 months of historical volume readings that can be accessed as attributes
  // in Home Assistant. Each value represents the total volume at the end of that month.
  // The meter stores these internally with timestamps, but the RADIAN protocol only
  // returns the volume values without dates.
  if (meter_data.history_available) {
    // Count valid historical values (non-zero)
    int num_history = 0;
    for (int i = 0; i < 13; i++) {
      if (meter_data.history[i] == 0) break;
      num_history++;
    }
    
    Serial.printf("\n=== Historical Data (%d months) ===\n", num_history);
    Serial.println("Month  Volume (L)  Usage (L)");
    Serial.println("-----  ----------  ---------");
    
    // Calculate monthly consumption from the historical data
    // Format: {"history": [oldest_volume, ..., newest_volume], "monthly_usage": [month1_usage, ..., month13_usage]}
    char historyJson[800];
    int pos = 0;
    
    // Start JSON object
    pos += snprintf(historyJson + pos, sizeof(historyJson) - pos, "{\"history\":[");
    
    // Add historical volumes and print to serial
    for (int i = 0; i < num_history; i++) {
      pos += snprintf(historyJson + pos, sizeof(historyJson) - pos, "%s%u", 
                      (i > 0 ? "," : ""), meter_data.history[i]);
      
      // Calculate and display monthly usage
      uint32_t usage = 0;
      if (i > 0 && meter_data.history[i] > meter_data.history[i - 1]) {
        usage = meter_data.history[i] - meter_data.history[i - 1];
      }
      Serial.printf(" -%02d   %10u  %9u\n", num_history-i, meter_data.history[i], usage);
    }
    
    // Calculate current month usage (difference from most recent historical reading)
    uint32_t currentMonthUsage = 0;
    if (num_history > 0 && meter_data.liters > meter_data.history[num_history - 1]) {
      currentMonthUsage = meter_data.liters - meter_data.history[num_history - 1];
    }
    Serial.printf("  Now  %10d  %9u (current month)\n", meter_data.liters, currentMonthUsage);
    Serial.println("===================================\n");
    
    // Add monthly usage calculations to JSON
    pos += snprintf(historyJson + pos, sizeof(historyJson) - pos, "],\"monthly_usage\":[");
    for (int i = 0; i < num_history; i++) {
      uint32_t usage;
      if (i == 0) {
        // For oldest month, we can't calculate usage without an older baseline
        usage = 0;
      } else if (meter_data.history[i] > meter_data.history[i - 1]) {
        // Calculate consumption as difference between consecutive months
        usage = meter_data.history[i] - meter_data.history[i - 1];
      } else {
        usage = 0; // Sanity check - shouldn't go backwards
      }
      pos += snprintf(historyJson + pos, sizeof(historyJson) - pos, "%s%u", 
                      (i > 0 ? "," : ""), usage);
    }
    
    pos += snprintf(historyJson + pos, sizeof(historyJson) - pos, 
                    "],\"current_month_usage\":%u,\"months_available\":%d}", 
                    currentMonthUsage, num_history);
    
    Serial.printf("Publishing JSON attributes (%d bytes): %s\n\n", strlen(historyJson), historyJson);
    mqtt.publish("everblu/cyble/liters_attributes", historyJson, true);
    delay(5);
    
    Serial.printf("> Published %d months historical data (current month usage: %u L)\n", 
                  num_history, currentMonthUsage);
  }
  
  snprintf(valueBuffer, sizeof(valueBuffer), "%d", meter_data.reads_counter);
  mqtt.publish("everblu/cyble/counter", valueBuffer, true);
  delay(5);
  
  snprintf(valueBuffer, sizeof(valueBuffer), "%d", meter_data.battery_left);
  mqtt.publish("everblu/cyble/battery", valueBuffer, true);
  delay(5);
  
  snprintf(valueBuffer, sizeof(valueBuffer), "%d", meter_data.rssi_dbm);
  mqtt.publish("everblu/cyble/rssi_dbm", valueBuffer, true);
  delay(5);
  
  snprintf(valueBuffer, sizeof(valueBuffer), "%d", calculateMeterdBmToPercentage(meter_data.rssi_dbm));
  mqtt.publish("everblu/cyble/rssi_percentage", valueBuffer, true);
  delay(5);
  
  snprintf(valueBuffer, sizeof(valueBuffer), "%d", meter_data.lqi);
  mqtt.publish("everblu/cyble/lqi", valueBuffer, true);
  delay(5);
  mqtt.publish("everblu/cyble/time_start", timeStartFormatted, true);
  delay(5);
  mqtt.publish("everblu/cyble/time_end", timeEndFormatted, true);
  delay(5);
  mqtt.publish("everblu/cyble/timestamp", iso8601, true); // timestamp since epoch in UTC
  delay(5);
  
  snprintf(valueBuffer, sizeof(valueBuffer), "%d", calculateLQIToPercentage(meter_data.lqi));
  mqtt.publish("everblu/cyble/lqi_percentage", valueBuffer, true);
  delay(5);

  // Publish all data as a JSON message as well this is redundant but may be useful for some
  char json[512];
  sprintf(json, jsonTemplate, meter_data.liters, meter_data.reads_counter, meter_data.battery_left, meter_data.rssi, iso8601);
  mqtt.publish("everblu/cyble/json", json, true);
  delay(5);

#if AUTO_ALIGN_READING_TIME
  // Optionally auto-align the daily scheduled reading time to the meter's wake window
  {
    int timeStart = constrain(meter_data.time_start, 0, 23);
    int timeEnd = constrain(meter_data.time_end, 0, 23);
    int window = (timeEnd - timeStart + 24) % 24; // hours in window (0 means unknown/all-day)

    if (window > 0) {
#if AUTO_ALIGN_USE_MIDPOINT
      int alignedHour = (timeStart + (window / 2)) % 24; // midpoint
#else
      int alignedHour = timeStart; // start of window
#endif
      // Update resolved schedule time (keep minutes as configured default)
      g_readHourUtc = alignedHour;
      g_readMinuteUtc = DEFAULT_READING_MINUTE_UTC;

      // Publish updated reading_time HH:MM
      char readingTimeFormatted2[6];
      snprintf(readingTimeFormatted2, sizeof(readingTimeFormatted2), "%02d:%02d", g_readHourUtc, g_readMinuteUtc);
      mqtt.publish("everblu/cyble/reading_time", readingTimeFormatted2, true);
      delay(5);

      Serial.printf("> Auto-aligned reading time to %02d:%02d UTC (window %02d-%02d)\n", g_readHourUtc, g_readMinuteUtc, timeStart, timeEnd);
    }
  }
#endif

  // Notify MQTT that active reading has ended
  mqtt.publish("everblu/cyble/active_reading", "false", true);
  mqtt.publish("everblu/cyble/cc1101_state", "Idle", true);
  digitalWrite(LED_BUILTIN, HIGH); // Turn off LED to indicate completion

  // Reset retry counter and cooldown on successful read
  _retry = 0;
  lastFailedAttempt = 0;
  successfulReads++;
  lastErrorMessage = "None";

  // Publish success metrics (using char buffers instead of String)
  char metricBuffer[16];
  
  snprintf(metricBuffer, sizeof(metricBuffer), "%lu", successfulReads);
  mqtt.publish("everblu/cyble/successful_reads", metricBuffer, true);
  
  snprintf(metricBuffer, sizeof(metricBuffer), "%lu", totalReadAttempts);
  mqtt.publish("everblu/cyble/total_attempts", metricBuffer, true);
  
  mqtt.publish("everblu/cyble/last_error", "None", true);

  // Perform adaptive frequency tracking based on FREQEST register
  adaptiveFrequencyTracking(meter_data.freqest);

  Serial.printf("Data update complete.\n\n");
}

// Function: onScheduled
// Description: Schedules daily meter readings at 10:00 AM UTC.
void onScheduled()
{
  time_t tnow = time(nullptr);
  struct tm *ptm = gmtime(&tnow);

  // Check if today is a valid reading day
  if (isReadingDay(ptm) && ptm->tm_hour == g_readHourUtc && ptm->tm_min == g_readMinuteUtc && ptm->tm_sec == 0) {
    // Check if we're still in cooldown period after failed attempts
    if (lastFailedAttempt > 0 && (millis() - lastFailedAttempt) < RETRY_COOLDOWN) {
      unsigned long remainingCooldown = (RETRY_COOLDOWN - (millis() - lastFailedAttempt)) / 1000;
      Serial.printf("Still in cooldown period. %lu seconds remaining.\n", remainingCooldown);
      
      char cooldownMsg[64];
      snprintf(cooldownMsg, sizeof(cooldownMsg), "Cooldown active, %lus remaining", remainingCooldown);
      mqtt.publish("everblu/cyble/status_message", cooldownMsg, true);
      mqtt.executeDelayed(500, onScheduled);
      return;
    }
    
    // Cooldown period is over, reset and proceed
    lastFailedAttempt = 0;
    
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

// Device information - reused in all discovery messages to reduce repetition
// Macro to embed device info in discovery JSON strings
#define DEVICE_JSON \
    "\"ids\": [\"14071984\"],\n" \
    "    \"name\": \"Water Meter\",\n" \
    "    \"mdl\": \"Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32\",\n" \
    "    \"mf\": \"Psykokwak [Forked by Genestealer]\""

// JSON Discovery for Reading (Total)
// This is used to show the total water usage in Home Assistant
const char jsonDiscoveryReading[] PROGMEM = R"rawliteral(
{
  "name": "Reading (Total)",
  "uniq_id": "water_meter_value",
  "obj_id": "water_meter_value",
  "ic": "mdi:water",
  "unit_of_meas": "L",
  "dev_cla": "water",
  "stat_cla": "total_increasing",
  "qos": 0,
  "sug_dsp_prc": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/liters",
  "json_attr_t": "everblu/cyble/liters_attributes",
  "frc_upd": true,
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// Note: Battery percentage sensor removed to comply with HA docs.
// We only expose months remaining (see jsonDiscoveryBatteryMonths) because the
// meter reports months, not percent. Mapping months->% would be arbitrary.

// JSON Discovery for Read Counter
// This is used to show the number of times the meter has been read in Home Assistant
const char jsonDiscoveryReadCounter[] PROGMEM = R"rawliteral(
{
  "name": "Read Counter",
  "uniq_id": "water_meter_counter",
  "obj_id": "water_meter_counter",
  "ic": "mdi:counter",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/counter",
  "frc_upd": true,
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Last Read (timestamp)
// This is used to show the last time the meter was read in Home Assistant
const char jsonDiscoveryLastRead[] PROGMEM = R"rawliteral(
{
  "name": "Last Read",
  "uniq_id": "water_meter_timestamp",
  "obj_id": "water_meter_timestamp",
  "dev_cla": "timestamp",
  "ic": "mdi:clock",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/timestamp",
  "frc_upd": true,
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Request Reading (button)
// This is used to trigger a reading from the meter when pressed in Home Assistant
const char jsonDiscoveryRequestReading[] PROGMEM = R"rawliteral(
{
  "name": "Request Reading Now",
  "uniq_id": "water_meter_request",
  "obj_id": "water_meter_request",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "cmd_t": "everblu/cyble/trigger",
  "pl_avail": "online",
  "pl_not_avail": "offline",
  "pl_prs": "update",
  "frc_upd": true,
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Active Reading (binary sensor)
// This is used to indicate that the device is currently reading data from the meter
const char jsonDiscoveryActiveReading[] PROGMEM = R"rawliteral(
{
  "name": "Active Reading",
  "uniq_id": "water_meter_active_reading",
  "obj_id": "water_meter_active_reading",
  "dev_cla": "running",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/active_reading",
  "pl_on": "true",
  "pl_off": "false",
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Wi-Fi Details
// These are used to provide information about the Wi-Fi connection of the device
const char jsonDiscoveryWifiIP[] PROGMEM = R"rawliteral(
{
  "name": "IP Address",
  "uniq_id": "water_meter_wifi_ip",
  "obj_id": "water_meter_wifi_ip",
  "ic": "mdi:ip-network-outline",
  "qos": 0,
  "stat_t": "everblu/cyble/wifi_ip",
  "frc_upd": true,
  "ent_cat": "diagnostic",
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Reading Time (HH:MM, UTC)
const char jsonDiscoveryReadingTime[] PROGMEM = R"rawliteral(
{
  "name": "Reading Time (UTC)",
  "uniq_id": "water_meter_reading_time",
  "obj_id": "water_meter_reading_time",
  "ic": "mdi:clock-outline",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/reading_time",
  "frc_upd": true,
  "ent_cat": "diagnostic",
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Wi-Fi RSSI
// This is used to show the Wi-Fi signal strength in Home Assistant
const char jsonDiscoveryWifiRSSI[] PROGMEM = R"rawliteral(
{
  "name": "WiFi RSSI",
  "uniq_id": "water_meter_wifi_rssi",
  "obj_id": "water_meter_wifi_rssi",
  "dev_cla": "signal_strength",
  "ic": "mdi:signal-variant",
  "unit_of_meas": "dBm",
  "stat_cla": "measurement",
  "qos": 0,
  "stat_t": "everblu/cyble/wifi_rssi",
  "frc_upd": true,
  "ent_cat": "diagnostic",
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Wi-Fi Signal Percentage
// This is used to show the Wi-Fi signal strength as a percentage in Home Assistant
const char jsonDiscoveryWifiSignalPercentage[] PROGMEM = R"rawliteral(
{
  "name": "WiFi Signal",
  "uniq_id": "water_meter_wifi_signal_percentage",
  "obj_id": "water_meter_wifi_signal_percentage",
  "ic": "mdi:wifi",
  "unit_of_meas": "%",
  "stat_cla": "measurement",
  "qos": 0,
  "stat_t": "everblu/cyble/wifi_signal_percentage",
  "frc_upd": true,
  "ent_cat": "diagnostic",
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for MAC Address
// This is used to show the MAC address of the device in Home Assistant
const char jsonDiscoveryMacAddress[] PROGMEM = R"rawliteral(
{
  "name": "MAC Address",
  "uniq_id": "water_meter_mac_address",
  "obj_id": "water_meter_mac_address",
  "ic": "mdi:network",
  "qos": 0,
  "stat_t": "everblu/cyble/mac_address",
  "frc_upd": true,
  "ent_cat": "diagnostic",
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for BSSID
// This is used to show the BSSID of the device in Home Assistant
const char jsonDiscoveryBSSID[] PROGMEM = R"rawliteral(
{
  "name": "WiFi BSSID",
  "uniq_id": "water_meter_wifi_bssid",
  "obj_id": "water_meter_wifi_bssid",
  "ic": "mdi:access-point-network",
  "qos": 0,
  "stat_t": "everblu/cyble/bssid",
  "frc_upd": true,
  "ent_cat": "diagnostic",
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Wi-Fi SSID
// This is used to show the SSID of the device in Home Assistant
const char jsonDiscoverySSID[] PROGMEM = R"rawliteral(
{
  "name": "WiFi SSID",
  "uniq_id": "water_meter_wifi_ssid",
  "obj_id": "water_meter_wifi_ssid",
  "ic": "mdi:help-network-outline",
  "qos": 0,
  "stat_t": "everblu/cyble/ssid",
  "frc_upd": true,
  "ent_cat": "diagnostic",
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Uptime
// This is used to show the uptime of the device in Home Assistant
const char jsonDiscoveryUptime[] PROGMEM = R"rawliteral(
{
  "name": "Device Uptime",
  "uniq_id": "water_meter_uptime",
  "obj_id": "water_meter_uptime",
  "dev_cla": "timestamp",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/uptime",
  "frc_upd": true,
  "ent_cat": "diagnostic",
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Restart Button
// This is used to trigger a restart of the device when pressed in Home Assistant
const char jsonDiscoveryRestartButton[] PROGMEM = R"rawliteral(
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
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Meter Year
const char jsonDiscoveryMeterYear[] PROGMEM = R"rawliteral(
{
  "name": "Meter Year",
  "uniq_id": "water_meter_year",
  "obj_id": "water_meter_year",
  "ic": "mdi:calendar",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/water_meter_year",
  "frc_upd": true,
  "ent_cat": "diagnostic",
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Meter Serial
const char jsonDiscoveryMeterSerial[] PROGMEM = R"rawliteral(
{
  "name": "Meter Serial",
  "uniq_id": "water_meter_serial",
  "obj_id": "water_meter_serial",
  "ic": "mdi:barcode",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/water_meter_serial",
  "frc_upd": true,
  "ent_cat": "diagnostic",
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Reading Schedule
const char jsonDiscoveryReadingSchedule[] PROGMEM = R"rawliteral(
{
  "name": "Reading Schedule",
  "uniq_id": "water_meter_reading_schedule",
  "obj_id": "water_meter_reading_schedule",
  "ic": "mdi:calendar-clock",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/reading_schedule",
  "frc_upd": true,
  "ent_cat": "diagnostic",
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Battery Months Left
const char jsonDiscoveryBatteryMonths[] PROGMEM = R"rawliteral(
{
  "name": "Months Remaining",
  "uniq_id": "water_meter_battery_months",
  "obj_id": "water_meter_battery_months",
  "ic": "mdi:battery-clock",
  "unit_of_meas": "months",
  "stat_cla": "measurement",
  "sug_dsp_prc": 0,
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/battery",
  "frc_upd": true,
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Meter RSSI (dBm)
const char jsonDiscoveryMeterRSSIDBm[] PROGMEM = R"rawliteral(
{
  "name": "RSSI",
  "uniq_id": "water_meter_rssi_dbm",
  "obj_id": "water_meter_rssi_dbm",
  "dev_cla": "signal_strength",
  "ic": "mdi:signal",
  "unit_of_meas": "dBm",
  "stat_cla": "measurement",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/rssi_dbm",
  "frc_upd": true,
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Meter RSSI (Percentage)
const char jsonDiscoveryMeterRSSIPercentage[] PROGMEM = R"rawliteral(
{
  "name": "Signal",
  "uniq_id": "water_meter_rssi_percentage",
  "obj_id": "water_meter_rssi_percentage",
  "ic": "mdi:signal-cellular-3",
  "unit_of_meas": "%",
  "stat_cla": "measurement",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/rssi_percentage",
  "frc_upd": true,
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Meter LQI (Link Quality Indicator)
const char jsonDiscoveryLQIPercentage[] PROGMEM = R"rawliteral(
  {
    "name": "Signal Quality (LQI)",
    "uniq_id": "water_meter_lqi_percentage",
    "obj_id": "water_meter_lqi_percentage",
    "ic": "mdi:signal-cellular-outline",
    "unit_of_meas": "%",
    "stat_cla": "measurement",
    "qos": 0,
    "avty_t": "everblu/cyble/status",
    "stat_t": "everblu/cyble/lqi_percentage",
    "frc_upd": true,
    "dev": {
      "ids": ["14071984"],
      "name": "Water Meter",
      "mdl": "Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32",
      "mf": "Psykokwak [Forked by Genestealer]"
    }
  }
  )rawliteral";
  
// JSON Discovery for Meter Wake Time
const char jsonDiscoveryTimeStart[] PROGMEM = R"rawliteral(
{
  "name": "Wake Time",
  "uniq_id": "water_meter_time_start",
  "obj_id": "water_meter_time_start",
  "ic": "mdi:clock-start",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/time_start",
  "frc_upd": true,
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Meter Sleep Time
const char jsonDiscoveryTimeEnd[] PROGMEM = R"rawliteral(
{
  "name": "Sleep Time",
  "uniq_id": "water_meter_time_end",
  "obj_id": "water_meter_time_end",
  "ic": "mdi:clock-end",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/time_end",
  "frc_upd": true,
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Total Read Attempts
const char jsonDiscoveryTotalAttempts[] PROGMEM = R"rawliteral(
{
  "name": "Total Read Attempts",
  "uniq_id": "water_meter_total_attempts",
  "obj_id": "water_meter_total_attempts",
  "ic": "mdi:counter",
  "stat_cla": "total_increasing",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/total_attempts",
  "frc_upd": true,
  "ent_cat": "diagnostic",
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Successful Reads
const char jsonDiscoverySuccessfulReads[] PROGMEM = R"rawliteral(
{
  "name": "Successful Reads",
  "uniq_id": "water_meter_successful_reads",
  "obj_id": "water_meter_successful_reads",
  "ic": "mdi:check-circle",
  "stat_cla": "total_increasing",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/successful_reads",
  "frc_upd": true,
  "ent_cat": "diagnostic",
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Failed Reads
const char jsonDiscoveryFailedReads[] PROGMEM = R"rawliteral(
{
  "name": "Failed Reads",
  "uniq_id": "water_meter_failed_reads",
  "obj_id": "water_meter_failed_reads",
  "ic": "mdi:alert-circle",
  "stat_cla": "total_increasing",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/failed_reads",
  "frc_upd": true,
  "ent_cat": "diagnostic",
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Last Error Message
const char jsonDiscoveryLastError[] PROGMEM = R"rawliteral(
{
  "name": "Last Error",
  "uniq_id": "water_meter_last_error",
  "obj_id": "water_meter_last_error",
  "ic": "mdi:alert",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/last_error",
  "frc_upd": true,
  "ent_cat": "diagnostic",
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for CC1101 State
const char jsonDiscoveryCC1101State[] PROGMEM = R"rawliteral(
{
  "name": "CC1101 State",
  "uniq_id": "water_meter_cc1101_state",
  "obj_id": "water_meter_cc1101_state",
  "ic": "mdi:radio-tower",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/cc1101_state",
  "frc_upd": true,
  "ent_cat": "diagnostic",
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Frequency Offset
const char jsonDiscoveryFreqOffset[] PROGMEM = R"rawliteral(
{
  "name": "Frequency Offset",
  "uniq_id": "water_meter_freq_offset",
  "obj_id": "water_meter_freq_offset",
  "ic": "mdi:sine-wave",
  "unit_of_meas": "kHz",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "stat_t": "everblu/cyble/frequency_offset",
  "frc_upd": true,
  "ent_cat": "diagnostic",
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";

// JSON Discovery for Frequency Scan Button
const char jsonDiscoveryFreqScanButton[] PROGMEM = R"rawliteral(
{
  "name": "Scan Frequency",
  "uniq_id": "water_meter_freq_scan",
  "obj_id": "water_meter_freq_scan",
  "ic": "mdi:magnify-scan",
  "qos": 0,
  "avty_t": "everblu/cyble/status",
  "cmd_t": "everblu/cyble/frequency_scan",
  "pl_prs": "scan",
  "ent_cat": "config",
  "dev": {
    )rawliteral" DEVICE_JSON R"rawliteral(
  }
}
)rawliteral";




// Function: publishWifiDetails
// Description: Publishes Wi-Fi diagnostics (IP, RSSI, signal strength, etc.) to MQTT.
void publishWifiDetails() {
  Serial.println("> Publish Wi-Fi details");
  
  // Get WiFi details (use String for network functions that return String)
  char wifiIP[16];
  snprintf(wifiIP, sizeof(wifiIP), "%s", WiFi.localIP().toString().c_str());
  
  int wifiRSSI = WiFi.RSSI();
  int wifiSignalPercentage = calculateWiFiSignalStrengthPercentage(wifiRSSI);
  
  char macAddress[18];
  snprintf(macAddress, sizeof(macAddress), "%s", WiFi.macAddress().c_str());
  
  char wifiSSID[33];
  snprintf(wifiSSID, sizeof(wifiSSID), "%s", WiFi.SSID().c_str());
  
  char wifiBSSID[18];
  snprintf(wifiBSSID, sizeof(wifiBSSID), "%s", WiFi.BSSIDstr().c_str());
  
  const char* status = (WiFi.status() == WL_CONNECTED) ? "online" : "offline";

  // Uptime calculation
  unsigned long uptimeMillis = millis();
  time_t uptimeSeconds = uptimeMillis / 1000;
  time_t now = time(nullptr);
  time_t uptimeTimestamp = now - uptimeSeconds;
  char uptimeISO[32];
  strftime(uptimeISO, sizeof(uptimeISO), "%FT%TZ", gmtime(&uptimeTimestamp));

  // Publish diagnostic sensors (using char buffers instead of String)
  char valueBuffer[16];
  
  mqtt.publish("everblu/cyble/wifi_ip", wifiIP, true);
  delay(5);
  
  snprintf(valueBuffer, sizeof(valueBuffer), "%d", wifiRSSI);
  mqtt.publish("everblu/cyble/wifi_rssi", valueBuffer, true);
  delay(5);
  
  snprintf(valueBuffer, sizeof(valueBuffer), "%d", wifiSignalPercentage);
  mqtt.publish("everblu/cyble/wifi_signal_percentage", valueBuffer, true);
  delay(5);
  
  mqtt.publish("everblu/cyble/mac_address", macAddress, true);
  delay(5);
  mqtt.publish("everblu/cyble/ssid", wifiSSID, true);
  delay(5);
  mqtt.publish("everblu/cyble/bssid", wifiBSSID, true);
  delay(5);
  mqtt.publish("everblu/cyble/status", status, true);
  delay(5);
  mqtt.publish("everblu/cyble/uptime", uptimeISO, true);
  delay(5);

  Serial.println("> Wi-Fi details published");
}

// Function: publishMeterSettings
// Description: Publishes meter configuration (year, serial, frequency) to MQTT.
void publishMeterSettings() {
  Serial.println("> Publish meter settings");

  // Publish Meter Year, Serial (using char buffers instead of String)
  char valueBuffer[16];
  
  snprintf(valueBuffer, sizeof(valueBuffer), "%d", METER_YEAR);
  mqtt.publish("everblu/cyble/water_meter_year", valueBuffer, true);
  delay(5);
  
  snprintf(valueBuffer, sizeof(valueBuffer), "%u", METER_SERIAL);
  mqtt.publish("everblu/cyble/water_meter_serial", valueBuffer, true);
  delay(5);

  // Publish Reading Schedule
  mqtt.publish("everblu/cyble/reading_schedule", readingSchedule, true);
  delay(5);

  // Publish Reading Time (UTC) as HH:MM text (resolved time that may be auto-aligned)
  char readingTimeFormatted[6];
  snprintf(readingTimeFormatted, sizeof(readingTimeFormatted), "%02d:%02d", (int)g_readHourUtc, (int)g_readMinuteUtc);
  mqtt.publish("everblu/cyble/reading_time", readingTimeFormatted, true);
  delay(5);

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
  Serial.printf("Current date (UTC) : %04d/%02d/%02d %02d:%02d/%02d - %ld\n", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec, (long)tnow);
  
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
    // Input validation: only accept whitelisted commands
    if (message != "update" && message != "read") {
      Serial.printf("WARN: Invalid trigger command '%s' (expected 'update' or 'read')\n", message.c_str());
      mqtt.publish("everblu/cyble/status_message", "Invalid trigger command", true);
      return;
    }
    
    // Check if we're in cooldown period
    if (lastFailedAttempt > 0 && (millis() - lastFailedAttempt) < RETRY_COOLDOWN) {
      unsigned long remainingCooldown = (RETRY_COOLDOWN - (millis() - lastFailedAttempt)) / 1000;
      Serial.printf("Cannot trigger update: Still in cooldown period. %lu seconds remaining.\n", remainingCooldown);
      
      char cooldownMsg[64];
      snprintf(cooldownMsg, sizeof(cooldownMsg), "Cooldown active, %lus remaining", remainingCooldown);
      mqtt.publish("everblu/cyble/status_message", cooldownMsg, true);
      return;
    }

    Serial.printf("Update data from meter from MQTT trigger (command: %s)\n", message.c_str());

    _retry = 0;
    onUpdateData();
  });

  mqtt.subscribe("everblu/cyble/restart", [](const String& message) {
    // Input validation: only accept exact "restart" command
    if (message != "restart") {
      Serial.printf("WARN: Invalid restart command '%s' (expected 'restart')\n", message.c_str());
      mqtt.publish("everblu/cyble/status_message", "Invalid restart command", true);
      return;
    }
    
    Serial.println("Restart command received via MQTT. Restarting in 2 seconds...");
    mqtt.publish("everblu/cyble/status_message", "Device restarting...", true);
    delay(2000); // Give time for MQTT message to be sent
    ESP.restart(); // Restart the ESP device
  });

  mqtt.subscribe("everblu/cyble/frequency_scan", [](const String& message) {
    // Input validation: only accept "scan" command
    if (message != "scan") {
      Serial.printf("WARN: Invalid frequency scan command '%s' (expected 'scan')\n", message.c_str());
      mqtt.publish("everblu/cyble/status_message", "Invalid scan command", true);
      return;
    }
    
    Serial.println("Frequency scan command received via MQTT");
    performFrequencyScan();
  });

  Serial.println("> Send MQTT config for HA.");

  // Publish Meter details discovery configuration
  delay(5);
  mqtt.publish("homeassistant/sensor/water_meter_value/config", FPSTR(jsonDiscoveryReading), true);
  delay(5);
  mqtt.publish("homeassistant/sensor/water_meter_counter/config", FPSTR(jsonDiscoveryReadCounter), true);
  delay(5);
  mqtt.publish("homeassistant/sensor/water_meter_timestamp/config", FPSTR(jsonDiscoveryLastRead), true);
  delay(5);
  mqtt.publish("homeassistant/button/water_meter_request/config", FPSTR(jsonDiscoveryRequestReading), true);
  delay(5);

  // Publish Wi-Fi details discovery configuration
  mqtt.publish("homeassistant/sensor/water_meter_wifi_ip/config", FPSTR(jsonDiscoveryWifiIP), true);
  delay(5);
  mqtt.publish("homeassistant/sensor/water_meter_wifi_rssi/config", FPSTR(jsonDiscoveryWifiRSSI), true);
  delay(5);
  mqtt.publish("homeassistant/sensor/water_meter_mac_address/config", FPSTR(jsonDiscoveryMacAddress), true);
  delay(5);
  mqtt.publish("homeassistant/sensor/water_meter_wifi_ssid/config", FPSTR(jsonDiscoverySSID), true);
  delay(5);
  mqtt.publish("homeassistant/sensor/water_meter_wifi_bssid/config", FPSTR(jsonDiscoveryBSSID), true);
  delay(5);
  mqtt.publish("homeassistant/sensor/water_meter_uptime/config", FPSTR(jsonDiscoveryUptime), true);
  delay(5);
  mqtt.publish("homeassistant/sensor/water_meter_wifi_signal_percentage/config", FPSTR(jsonDiscoveryWifiSignalPercentage), true);
  delay(5);

  // Publish MQTT discovery messages for the Restart Button
  mqtt.publish("homeassistant/button/water_meter_restart/config", FPSTR(jsonDiscoveryRestartButton), true);
  delay(5);

  // Publish MQTT discovery message for the binary sensor
  mqtt.publish("homeassistant/binary_sensor/water_meter_active_reading/config", FPSTR(jsonDiscoveryActiveReading), true);
  delay(5);

  // Publish MQTT discovery messages for Meter Year, Serial
  mqtt.publish("homeassistant/sensor/water_meter_year/config", FPSTR(jsonDiscoveryMeterYear), true);
  delay(5);
  mqtt.publish("homeassistant/sensor/water_meter_serial/config", FPSTR(jsonDiscoveryMeterSerial), true);
  delay(5);

  // Publish JSON discovery for Reading Schedule
  mqtt.publish("homeassistant/sensor/water_meter_reading_schedule/config", FPSTR(jsonDiscoveryReadingSchedule), true);
  delay(5);

  // Publish JSON discovery for Battery Months Left
  mqtt.publish("homeassistant/sensor/water_meter_battery_months/config", FPSTR(jsonDiscoveryBatteryMonths), true);
  delay(5);

  // Publish JSON discovery for Meter RSSI (dBm), RSSI (%), and LQI (%)
  mqtt.publish("homeassistant/sensor/water_meter_rssi_dbm/config", FPSTR(jsonDiscoveryMeterRSSIDBm), true);
  delay(5);
  mqtt.publish("homeassistant/sensor/water_meter_rssi_percentage/config", FPSTR(jsonDiscoveryMeterRSSIPercentage), true);
  delay(5);
  mqtt.publish("homeassistant/sensor/water_meter_lqi_percentage/config", FPSTR(jsonDiscoveryLQIPercentage), true);
  delay(5);

  // Publish JSON discovery for the times the meter wakes and sleeps
  mqtt.publish("homeassistant/sensor/water_meter_time_start/config", FPSTR(jsonDiscoveryTimeStart), true);
  delay(5);
  mqtt.publish("homeassistant/sensor/water_meter_time_end/config", FPSTR(jsonDiscoveryTimeEnd), true);
  delay(5);

  // Publish JSON discovery for diagnostics and metrics
  mqtt.publish("homeassistant/sensor/water_meter_total_attempts/config", FPSTR(jsonDiscoveryTotalAttempts), true);
  delay(5);
  mqtt.publish("homeassistant/sensor/water_meter_successful_reads/config", FPSTR(jsonDiscoverySuccessfulReads), true);
  delay(5);
  mqtt.publish("homeassistant/sensor/water_meter_failed_reads/config", FPSTR(jsonDiscoveryFailedReads), true);
  delay(5);
  mqtt.publish("homeassistant/sensor/water_meter_last_error/config", FPSTR(jsonDiscoveryLastError), true);
  delay(5);
  mqtt.publish("homeassistant/sensor/water_meter_cc1101_state/config", FPSTR(jsonDiscoveryCC1101State), true);
  delay(5);
  mqtt.publish("homeassistant/sensor/water_meter_freq_offset/config", FPSTR(jsonDiscoveryFreqOffset), true);
  delay(5);
  mqtt.publish("homeassistant/button/water_meter_freq_scan/config", FPSTR(jsonDiscoveryFreqScanButton), true);
  delay(5);

  // Set initial state for active reading
  mqtt.publish("everblu/cyble/active_reading", "false", true);
  delay(5);
  
  // Publish initial diagnostic metrics (using char buffers instead of String)
  char metricBuffer[16];
  
  mqtt.publish("everblu/cyble/cc1101_state", "Idle", true);
  delay(5);
  
  snprintf(metricBuffer, sizeof(metricBuffer), "%lu", totalReadAttempts);
  mqtt.publish("everblu/cyble/total_attempts", metricBuffer, true);
  delay(5);
  
  snprintf(metricBuffer, sizeof(metricBuffer), "%lu", successfulReads);
  mqtt.publish("everblu/cyble/successful_reads", metricBuffer, true);
  delay(5);
  
  snprintf(metricBuffer, sizeof(metricBuffer), "%lu", failedReads);
  mqtt.publish("everblu/cyble/failed_reads", metricBuffer, true);
  delay(5);
  mqtt.publish("everblu/cyble/last_error", lastErrorMessage, true);
  delay(5);
  
  char freqBuffer[16];
  snprintf(freqBuffer, sizeof(freqBuffer), "%.3f", storedFrequencyOffset * 1000.0);  // Convert MHz to kHz
  mqtt.publish("everblu/cyble/frequency_offset", freqBuffer, true);
  delay(5);

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

// Function: saveFrequencyOffset
// Description: Saves the frequency offset to persistent storage (EEPROM for ESP8266, Preferences for ESP32)
void saveFrequencyOffset(float offset) {
#if defined(ESP8266)
  // ESP8266: Use EEPROM
  uint16_t magic = FREQ_OFFSET_MAGIC;
  EEPROM.put(FREQ_OFFSET_ADDR, magic);
  EEPROM.put(FREQ_OFFSET_ADDR + 2, offset);
  EEPROM.commit();
  Serial.printf("> Frequency offset %.6f MHz saved to EEPROM\n", offset);
#elif defined(ESP32)
  // ESP32: Use Preferences
  preferences.begin("everblu", false);
  preferences.putFloat("freq_offset", offset);
  preferences.end();
  Serial.printf("> Frequency offset %.6f MHz saved to Preferences\n", offset);
#endif
  storedFrequencyOffset = offset;
}

// Function: loadFrequencyOffset
// Description: Loads the frequency offset from persistent storage, returns 0.0 if not found or invalid
float loadFrequencyOffset() {
#if defined(ESP8266)
  // ESP8266: Use EEPROM
  uint16_t magic = 0;
  EEPROM.get(FREQ_OFFSET_ADDR, magic);
  if (magic == FREQ_OFFSET_MAGIC) {
    float offset = 0.0;
    EEPROM.get(FREQ_OFFSET_ADDR + 2, offset);
    // Sanity check: offset should be reasonable (within ±0.1 MHz)
    if (offset >= -0.1 && offset <= 0.1) {
      Serial.printf("> Loaded frequency offset %.6f MHz from EEPROM\n", offset);
      return offset;
    } else {
      Serial.printf("> Invalid frequency offset %.6f MHz in EEPROM, using 0.0\n", offset);
    }
  } else {
    Serial.println("> No valid frequency offset found in EEPROM");
  }
#elif defined(ESP32)
  // ESP32: Use Preferences
  preferences.begin("everblu", true); // Read-only
  if (preferences.isKey("freq_offset")) {
    float offset = preferences.getFloat("freq_offset", 0.0);
    preferences.end();
    // Sanity check: offset should be reasonable (within ±0.1 MHz)
    if (offset >= -0.1 && offset <= 0.1) {
      Serial.printf("> Loaded frequency offset %.6f MHz from Preferences\n", offset);
      return offset;
    } else {
      Serial.printf("> Invalid frequency offset %.6f MHz in Preferences, using 0.0\n", offset);
    }
  } else {
    Serial.println("> No frequency offset found in Preferences");
    preferences.end();
  }
#endif
  return 0.0;
}

// Function: performFrequencyScan
// Description: Scans nearby frequencies to find the best signal and updates the offset
void performFrequencyScan() {
  Serial.println("> Starting frequency scan...");
  mqtt.publish("everblu/cyble/cc1101_state", "Frequency Scanning", true);
  mqtt.publish("everblu/cyble/status_message", "Performing frequency scan", true);
  
  float baseFreq = FREQUENCY;
  float bestFreq = baseFreq;
  int bestRSSI = -120; // Start with very low RSSI
  
  // Scan range: ±30 kHz in 5 kHz steps (±0.03 MHz in 0.005 MHz steps)
  float scanStart = baseFreq - 0.03;
  float scanEnd = baseFreq + 0.03;
  float scanStep = 0.005;
  
  Serial.printf("> Scanning from %.6f to %.6f MHz (step: %.6f MHz)\n", scanStart, scanEnd, scanStep);
  
  for (float freq = scanStart; freq <= scanEnd; freq += scanStep) {
    FEED_WDT(); // Feed watchdog for each frequency step
    // Reinitialize CC1101 with this frequency
    cc1101_init(freq);
    delay(50); // Allow time for frequency to settle
    
    // Try to get meter data (with short timeout)
    struct tmeter_data test_data = get_meter_data();
    
    if (test_data.rssi_dbm > bestRSSI && test_data.reads_counter > 0) {
      bestRSSI = test_data.rssi_dbm;
      bestFreq = freq;
      Serial.printf("> Better signal at %.6f MHz: RSSI=%d dBm\n", freq, test_data.rssi_dbm);
    }
  }
  
  // Calculate and save the offset
  float offset = bestFreq - baseFreq;
  Serial.printf("> Frequency scan complete. Best frequency: %.6f MHz (offset: %.6f MHz, RSSI: %d dBm)\n", 
                bestFreq, offset, bestRSSI);
  
  if (bestRSSI > -120) { // Only save if we found something
    saveFrequencyOffset(offset);
    
    char freqBuffer[16];
    snprintf(freqBuffer, sizeof(freqBuffer), "%.3f", offset * 1000.0);  // Convert MHz to kHz
    mqtt.publish("everblu/cyble/frequency_offset", freqBuffer, true);
    
    char statusMsg[128];
    snprintf(statusMsg, sizeof(statusMsg), "Scan complete: offset %.3f kHz, RSSI %d dBm", offset * 1000.0, bestRSSI);
    mqtt.publish("everblu/cyble/status_message", statusMsg, true);
    
    // Reinitialize with the best frequency
    cc1101_init(bestFreq);
  } else {
    Serial.println("> Frequency scan failed - no valid signal found");
    mqtt.publish("everblu/cyble/status_message", "Frequency scan failed - no signal", true);
    // Restore original frequency
    cc1101_init(baseFreq + storedFrequencyOffset);
  }
  
  mqtt.publish("everblu/cyble/cc1101_state", "Idle", true);
}

// Function: performWideInitialScan
// Description: Performs a wide-band scan on first boot to find meter frequency automatically
//              Scans ±100 kHz around the configured frequency in larger steps for faster discovery
void performWideInitialScan() {
  Serial.println("> Performing wide initial scan (first boot - no saved offset)...");
  mqtt.publish("everblu/cyble/cc1101_state", "Initial Frequency Scan", true);
  mqtt.publish("everblu/cyble/status_message", "First boot: scanning for meter frequency", true);
  
  float baseFreq = FREQUENCY;
  float bestFreq = baseFreq;
  int bestRSSI = -120;
  
  // Wide scan: ±100 kHz in 10 kHz steps for faster initial discovery
  float scanStart = baseFreq - 0.10;
  float scanEnd = baseFreq + 0.10;
  float scanStep = 0.010;
  
  Serial.printf("> Wide scan from %.6f to %.6f MHz (step: %.6f MHz)\n", scanStart, scanEnd, scanStep);
  Serial.println("> This may take 1-2 minutes on first boot...");
  
  for (float freq = scanStart; freq <= scanEnd; freq += scanStep) {
    FEED_WDT(); // Feed watchdog for each frequency step
    cc1101_init(freq);
    delay(100); // Longer delay for frequency to settle during wide scan
    
    struct tmeter_data test_data = get_meter_data();
    
    if (test_data.rssi_dbm > bestRSSI && test_data.reads_counter > 0) {
      bestRSSI = test_data.rssi_dbm;
      bestFreq = freq;
      Serial.printf("> Found signal at %.6f MHz: RSSI=%d dBm\n", freq, test_data.rssi_dbm);
    }
  }
  
  if (bestRSSI > -120) {
    // Found a signal, now do a fine scan around it
    Serial.printf("> Performing fine scan around %.6f MHz...\n", bestFreq);
    float fineStart = bestFreq - 0.015;
    float fineEnd = bestFreq + 0.015;
    float fineStep = 0.003;
    int fineBestRSSI = bestRSSI;
    float fineBestFreq = bestFreq;
    
    for (float freq = fineStart; freq <= fineEnd; freq += fineStep) {
      FEED_WDT(); // Feed watchdog for each frequency step
      cc1101_init(freq);
      delay(50);
      
      struct tmeter_data test_data = get_meter_data();
      
      if (test_data.rssi_dbm > fineBestRSSI && test_data.reads_counter > 0) {
        fineBestRSSI = test_data.rssi_dbm;
        fineBestFreq = freq;
        Serial.printf("> Refined signal at %.6f MHz: RSSI=%d dBm\n", freq, test_data.rssi_dbm);
      }
    }
    
    bestFreq = fineBestFreq;
    bestRSSI = fineBestRSSI;
    
    float offset = bestFreq - baseFreq;
    Serial.printf("> Initial scan complete! Best frequency: %.6f MHz (offset: %.6f MHz, RSSI: %d dBm)\n", 
                  bestFreq, offset, bestRSSI);
    
    saveFrequencyOffset(offset);
    
    char freqBuffer[16];
    snprintf(freqBuffer, sizeof(freqBuffer), "%.3f", offset * 1000.0);  // Convert MHz to kHz
    mqtt.publish("everblu/cyble/frequency_offset", freqBuffer, true);
    
    char statusMsg[128];
    snprintf(statusMsg, sizeof(statusMsg), "Initial scan complete: offset %.3f kHz", offset * 1000.0);
    mqtt.publish("everblu/cyble/status_message", statusMsg, true);
    
    cc1101_init(bestFreq);
  } else {
    Serial.println("> Wide scan failed - no meter signal found!");
    Serial.println("> Please check:");
    Serial.println(">  1. Meter is within range (< 50m typically)");
    Serial.println(">  2. Antenna is connected to CC1101");
    Serial.println(">  3. Meter serial/year are correct in private.h");
    Serial.println(">  4. Current time is within meter's wake hours");
    mqtt.publish("everblu/cyble/status_message", "Initial scan failed - check setup", true);
    cc1101_init(baseFreq);
  }
  
  mqtt.publish("everblu/cyble/cc1101_state", "Idle", true);
}

// Function: adaptiveFrequencyTracking
// Description: Uses FREQEST register to adaptively adjust frequency offset over time
//              Accumulates frequency error estimates and adjusts when threshold is reached
void adaptiveFrequencyTracking(int8_t freqest) {
  // FREQEST is a two's complement value representing frequency offset
  // Resolution is approximately Fxosc/2^14 ≈ 1.59 kHz per LSB (for 26 MHz crystal)
  const float FREQEST_TO_MHZ = 0.001587; // Conversion factor: ~1.59 kHz per LSB
  
  // Accumulate the frequency error
  float freqErrorMHz = (float)freqest * FREQEST_TO_MHZ;
  cumulativeFreqError += freqErrorMHz;
  successfulReadsBeforeAdapt++;
  
  Serial.printf("> FREQEST: %d (%.4f kHz error), cumulative: %.4f kHz over %d reads\n", 
                freqest, freqErrorMHz * 1000, cumulativeFreqError * 1000, successfulReadsBeforeAdapt);
  
  // Only adapt after N successful reads to avoid over-correcting on noise
  if (successfulReadsBeforeAdapt >= ADAPT_THRESHOLD) {
    float avgError = cumulativeFreqError / ADAPT_THRESHOLD;
    
    // Only adjust if average error is significant (> 2 kHz)
    if (abs(avgError * 1000) > 2.0) {
      Serial.printf("> Adaptive adjustment: average error %.4f kHz over %d reads\n", 
                    avgError * 1000, ADAPT_THRESHOLD);
      
      // Adjust the stored offset (apply 50% of the measured error to avoid over-correction)
      float adjustment = avgError * 0.5;
      storedFrequencyOffset += adjustment;
      
      Serial.printf("> Adjusting frequency offset by %.6f MHz (new offset: %.6f MHz)\n", 
                    adjustment, storedFrequencyOffset);
      
      saveFrequencyOffset(storedFrequencyOffset);
      
      char freqBuffer[16];
      snprintf(freqBuffer, sizeof(freqBuffer), "%.3f", storedFrequencyOffset * 1000.0);  // Convert MHz to kHz
      mqtt.publish("everblu/cyble/frequency_offset", freqBuffer, true);
      
      // Reinitialize CC1101 with adjusted frequency
      cc1101_init(FREQUENCY + storedFrequencyOffset);
    } else {
      Serial.printf("> Frequency stable (avg error %.4f kHz < 2 kHz threshold)\n", avgError * 1000);
    }
    
    // Reset accumulators
    cumulativeFreqError = 0.0;
    successfulReadsBeforeAdapt = 0;
  }
}


// Function: validateConfiguration
// Description: Validates configuration parameters at startup to fail fast on invalid settings
bool validateConfiguration() {
  bool valid = true;
  
  Serial.println("\n=== Configuration Validation ===");
  
  // Validate METER_YEAR (should be 0-99 for years 2000-2099)
  if (METER_YEAR > 99) {
    Serial.printf("ERROR: Invalid METER_YEAR=%d (expected 0-99)\n", METER_YEAR);
    valid = false;
  } else {
    Serial.printf("✓ METER_YEAR: %d (20%02d)\n", METER_YEAR, METER_YEAR);
  }
  
  // Validate METER_SERIAL (should not be 0)
  if (METER_SERIAL == 0) {
    Serial.println("ERROR: METER_SERIAL not configured (value is 0)");
    Serial.println("       Update METER_SERIAL in private.h with your meter's serial number");
    valid = false;
  } else {
    Serial.printf("✓ METER_SERIAL: %lu\n", (unsigned long)METER_SERIAL);
  }
  
  // Validate FREQUENCY if defined (should be 300-500 MHz for 433 MHz band)
  #ifdef FREQUENCY
  if (FREQUENCY < 300.0 || FREQUENCY > 500.0) {
    Serial.printf("ERROR: Invalid FREQUENCY=%.2f MHz (expected 300-500 MHz)\n", FREQUENCY);
    valid = false;
  } else {
    Serial.printf("✓ FREQUENCY: %.6f MHz\n", FREQUENCY);
  }
  #else
  Serial.println("✓ FREQUENCY: Using default 433.82 MHz (RADIAN protocol)");
  #endif
  
  // Validate GDO0 pin (basic check - should be defined)
  #ifdef GDO0
  Serial.printf("✓ GDO0 Pin: GPIO %d\n", GDO0);
  #else
  Serial.println("ERROR: GDO0 pin not defined in private.h");
  valid = false;
  #endif
  
  // Validate reading schedule
  if (!isValidReadingSchedule(readingSchedule)) {
    Serial.printf("ERROR: Invalid reading schedule '%s'\n", readingSchedule);
    Serial.println("       Expected: 'Monday-Friday', 'Monday-Saturday', or 'Monday-Sunday'");
    valid = false;
  } else {
    Serial.printf("✓ Reading Schedule: %s\n", readingSchedule);
  }
  
  Serial.println("================================\n");
  
  return valid;
}

// Function: setup
// Description: Initializes the device, including serial communication, Wi-Fi, MQTT, and CC1101 radio with automatic calibration.
void setup()
{
  Serial.begin(115200);
  Serial.println("\n");
  Serial.println("Everblu Meters ESP8266/ESP32 Starting...");
  Serial.println("Water usage data for Home Assistant");
  Serial.println("https://github.com/genestealer/everblu-meters-esp8266-improved");
  Serial.printf("Target meter: 20%02d-%07lu\n\n", METER_YEAR, (unsigned long)METER_SERIAL);

  // Validate configuration before proceeding
  if (!validateConfiguration()) {
    Serial.println("\n*** FATAL: Configuration validation failed! ***");
    Serial.println("*** Fix the errors in private.h and reflash ***");
    Serial.println("*** Device halted - will not continue ***\n");
    while(1) {
      digitalWrite(LED_BUILTIN, LOW);  // Blink LED to indicate error
      delay(200);
      digitalWrite(LED_BUILTIN, HIGH);
      delay(200);
    }
  }
  
  Serial.println("✓ Configuration valid - proceeding with initialization\n");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW); // turned on to start with

  // Initialize persistent storage
#if defined(ESP8266)
  EEPROM.begin(EEPROM_SIZE);
  Serial.println("> EEPROM initialized");
  
  // Clear EEPROM if requested (set CLEAR_EEPROM_ON_BOOT=1 in private.h)
  // Use this when replacing ESP board, CC1101 module, or moving to a different meter
  #if CLEAR_EEPROM_ON_BOOT
    Serial.println("> CLEARING EEPROM (CLEAR_EEPROM_ON_BOOT = 1)...");
    for (int i = 0; i < EEPROM_SIZE; i++) {
      EEPROM.write(i, 0xFF);
    }
    EEPROM.commit();
    Serial.println("> EEPROM cleared. Remember to set CLEAR_EEPROM_ON_BOOT = 0 after testing!");
  #endif
#endif

  // Load stored frequency offset
  storedFrequencyOffset = loadFrequencyOffset();

  // If no valid frequency offset found and auto-scan is enabled, perform wide initial scan
  if (storedFrequencyOffset == 0.0 && autoScanEnabled) {
    Serial.println("> No stored frequency offset found. Performing wide initial scan...");
    performWideInitialScan();
    // Reload the frequency offset after scan
    storedFrequencyOffset = loadFrequencyOffset();
  }

  // Increase the max packet size to handle large MQTT payloads
  mqtt.setMaxPacketSize(2048); // Set to a size larger than your longest payload

  // Set the Last Will and Testament (LWT)
  mqtt.enableLastWillMessage("everblu/cyble/status", "offline", true);  // You can activate the retain flag by setting the third parameter to true

  // Conditionally enable Wi-Fi PHY mode 11G (ESP8266 only).
  #if defined(ESP8266)
    #if ENABLE_WIFI_PHY_MODE_11G
    WiFi.setPhyMode(WIFI_PHY_MODE_11G);
    Serial.println("Wi-Fi PHY mode set to 11G.");
    #else
    Serial.println("> Wi-Fi PHY mode 11G is disabled.");
    #endif
  #else
    Serial.println("> Wi-Fi PHY mode setting not applicable on this platform.");
  #endif

  // Validate and log the configured reading schedule
  Serial.printf("> Reading schedule (configured): %s\n", readingSchedule);
  validateReadingSchedule();
  Serial.printf("> Reading schedule (effective): %s\n", readingSchedule);

  // Log effective frequency and warn if default is used
  Serial.printf("> Frequency (effective): %.6f MHz\n", (double)FREQUENCY);
#if FREQUENCY_DEFINED_DEFAULT
  Serial.println("WARNING: FREQUENCY not set in private.h; using default 433.820000 MHz (RADIAN).");
#endif

  // Optional functionalities of EspMQTTClient
  // mqtt.enableDebuggingMessages(true); // Enable debugging messages sent to serial output

  // Set CC1101 radio frequency with automatic calibration
  Serial.println("> Initializing CC1101 radio...");
  float effectiveFrequency = FREQUENCY + storedFrequencyOffset;
  if (storedFrequencyOffset != 0.0) {
    Serial.printf("> Applying stored frequency offset: %.6f MHz (effective: %.6f MHz)\n", 
                  storedFrequencyOffset, effectiveFrequency);
  }
  if (!cc1101_init(effectiveFrequency)) {
    Serial.println("FATAL ERROR: CC1101 radio initialization failed!");
    Serial.println("Please check your wiring and connections.");
    while (true) {
      digitalWrite(LED_BUILTIN, LOW);  // Blink LED to indicate error
      delay(200);
      digitalWrite(LED_BUILTIN, HIGH);
      delay(200);
    }
  }
  Serial.println("> CC1101 radio initialized successfully");

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

