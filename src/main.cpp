/*
 * Project: EverBlu Meters ESP8266/ESP32
 * Description: Fetch water/gas usage data from Itron EverBlu Cyble Enhanced RF water and gas meters using the RADIAN protocol on 433 MHz.
 *              Integrated with Home Assistant via MQTT AutoDiscovery.
 *              Supports both water meters (readings in L) and gas meters (readings in m³).
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

#include "private.h"             // Include private configuration (Wi-Fi, MQTT, etc.)
#include "cc1101.h"              // CC1101 radio driver and meter data functions
#include "version.h"             // Firmware version definition
#include "wifi_serial.h"         // WiFi serial monitor
#include "frequency_manager.h"   // Frequency management module
#include "storage_abstraction.h" // Platform-independent storage
#include "schedule_manager.h"    // Daily reading schedule management
#include "meter_history.h"       // Historical meter data processing
#if defined(ESP8266)
#include <ESP8266WiFi.h> // Wi-Fi library for ESP8266
#include <ESP8266mDNS.h> // mDNS library for ESP8266
#elif defined(ESP32)
#include <WiFi.h>         // Wi-Fi library for ESP32
#include <ESPmDNS.h>      // mDNS library for ESP32
#include <esp_task_wdt.h> // Watchdog timer for ESP32
#endif
#include <Arduino.h>       // Core Arduino library
#include <ArduinoOTA.h>    // OTA update library
#include <EspMQTTClient.h> // MQTT client library
#include <math.h>          // For floor/ceil during scan alignment

// Cross-platform watchdog feed helper
static inline void FEED_WDT()
{
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

// ------------------------------
// Connectivity watchdog settings
// ------------------------------
// Maximum time to wait for Wi-Fi to connect before forcing a retry
static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 30000UL; // 30s
// Maximum time to wait for MQTT to connect (with Wi-Fi up) before logging a warning
static const unsigned long MQTT_CONNECT_TIMEOUT_MS = 30000UL; // 30s
// If the device stays offline for too long, perform a safety reboot (set to 0 to disable)
static const unsigned long OFFLINE_REBOOT_AFTER_MS = 6UL * 60UL * 60UL * 1000UL; // 6 hours
// LED blink while offline (visual feedback)
static const unsigned long OFFLINE_LED_BLINK_MS = 500UL;

// Define the Wi-Fi PHY mode if missing from the private.h file
#ifndef ENABLE_WIFI_PHY_MODE_11G
#define ENABLE_WIFI_PHY_MODE_11G 0 // Set to 1 to enable 11G PHY mode
#endif

// Define MQTT debugging if missing from the private.h file
#ifndef ENABLE_MQTT_DEBUGGING
#define ENABLE_MQTT_DEBUGGING 0 // Set to 1 to enable MQTT debugging messages
#endif

// Define gas volume divisor if missing from the private.h file
// Converts internal liter count to cubic meters for gas meters
// Default: 100 (equivalent to 0.01 m³ per unit)
#ifndef GAS_VOLUME_DIVISOR
#define GAS_VOLUME_DIVISOR 100
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

// Simple time zone offset (minutes from UTC). Positive for east of UTC, negative for west.
#ifndef TIMEZONE_OFFSET_MINUTES
#define TIMEZONE_OFFSET_MINUTES 0
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

// Control whether the firmware performs the wide-band auto scan on first boot
// 1 = enabled (default), 0 = disabled
#ifndef AUTO_SCAN_ENABLED
#define AUTO_SCAN_ENABLED 1
#endif

// Resolved reading time (UTC+offset) managed by ScheduleManager
static bool g_isScheduledRead = false;

// Define a default meter frequency if missing from private.h.
// RADIAN protocol nominal center frequency for EverBlu is 433.82 MHz.
#ifndef FREQUENCY
#define FREQUENCY 433.82
#define FREQUENCY_DEFINED_DEFAULT 1
#else
#define FREQUENCY_DEFINED_DEFAULT 0
#endif

unsigned long lastWifiUpdate = 0;

// Schedule and timing configuration globals
int g_readHourLocal = 10;      // Scheduled read time in local time (hours)
int g_readMinuteLocal = 0;     // Scheduled read time in local time (minutes)
int g_readHourUtc = 10;        // Scheduled read time in UTC (hours)
int g_readMinuteUtc = 0;       // Scheduled read time in UTC (minutes)
char readingSchedule[32] = ""; // Reading schedule configuration (should be initialized in setup)
char meterSerialStr[16] = "";  // Meter serial number as string
char mqttBaseTopic[128] = "";  // MQTT base topic prefix
char mqttLwtTopic[128] = "";   // MQTT last-will-and-testament topic

// MQTT topic buffer size constant
#define MQTT_TOPIC_BUFFER_SIZE 128

// NTP synchronization status
bool g_ntpSynced = false; // Tracks whether NTP time has been successfully synchronized

// Read success/failure metrics
unsigned long totalReadAttempts = 0;
unsigned long successfulReads = 0;
unsigned long failedReads = 0;
unsigned long readSuccessCount = 0; // Current session success count
unsigned long readFailureCount = 0; // Current session failure count
const char *lastErrorMessage = "None";

// CC1101 radio connection state
bool cc1101RadioConnected = false; // Tracks whether the radio is detected and initialized

// Secrets pulled from private.h file
// Note: MQTT Client ID is made unique per device by appending the meter serial number
// This prevents multiple devices from having the same client ID and causing MQTT connection conflicts
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define MQTT_CLIENT_ID_WITH_SERIAL SECRET_MQTT_CLIENT_ID "-" TOSTRING(METER_SERIAL)

EspMQTTClient mqtt(
    SECRET_WIFI_SSID,           // Your Wifi SSID
    SECRET_WIFI_PASSWORD,       // Your WiFi key
    SECRET_MQTT_SERVER,         // MQTT Broker server ip
    SECRET_MQTT_USERNAME,       // MQTT Username Can be omitted if not needed
    SECRET_MQTT_PASSWORD,       // MQTT Password Can be omitted if not needed
    MQTT_CLIENT_ID_WITH_SERIAL, // MQTT Client name: uniquely identifies device by appending meter serial
    1883                        // MQTT Broker server port
);

// Base MQTT topic prefix for all EverBlu Cyble entities
// Centralising this avoids repeating "everblu/cyble/" all over the code.
// mqttBaseTopic is initialized at global scope using compile-time string
// concatenation with METER_SERIAL (via TOSTRING) and is only printed in setup().

const char jsonTemplate[] = "{ "
                            "\"liters\": %d, "
                            "\"counter\" : %d, "
                            "\"battery\" : %d, "
                            "\"rssi\" : %d, "
                            "\"timestamp\" : \"%s\" }";

// Define the default maximum retries if missing from the private.h file
#ifndef MAX_RETRIES
#define MAX_RETRIES 10 // Default: 10 retry attempts before cooldown
#endif

int _retry = 0;
const int max_retries = MAX_RETRIES;          // Maximum number of retry attempts (configurable in private.h)
unsigned long lastFailedAttempt = 0;          // Timestamp of last failed attempt
const unsigned long RETRY_COOLDOWN = 3600000; // 1 hour cooldown in milliseconds

// ============================================================================
// Meter Type Configuration
// ============================================================================

// Define meter type if not defined in private.h (default to water)
#ifndef METER_TYPE
#define METER_TYPE "water"
#endif

// Meter-specific configuration based on METER_TYPE
// These variables control the Home Assistant device class, icon, and unit of measurement
const char *meterDeviceClass;
const char *meterIcon;
const char *meterUnit;
// Compile-time string equality helper to avoid runtime strcmp for meter type
constexpr bool strEq(const char *a, const char *b)
{
  return (*a == *b) && (*a == '\0' || strEq(a + 1, b + 1));
}
constexpr bool meterIsGas = strEq(METER_TYPE, "gas");

// Initialize meter configuration
static void initMeterTypeConfig()
{
  if (meterIsGas)
  {
    meterDeviceClass = "gas";
    meterIcon = "mdi:meter-gas";
    meterUnit = "m³";
    Serial.println("> Meter type: GAS (readings in m³)");
  }
  else
  {
    meterDeviceClass = "water";
    meterIcon = "mdi:water";
    meterUnit = "L";
    Serial.println("> Meter type: WATER (readings in L)");
  }
}

// ============================================================================
// Schedule Validation API
// ============================================================================

/**
 * @brief Validate reading schedule string
 *
 * @param s Schedule string to validate
 * @return true if schedule is one of: "Monday-Friday", "Monday-Saturday", "Monday-Sunday"
 */
static bool isValidReadingSchedule(const char *s)
{
  // Moved to ScheduleManager - call ScheduleManager::isValidSchedule() instead
  return ScheduleManager::isValidSchedule(s);
}

/**
 * @brief Validate and correct reading schedule
 *
 * Checks if current reading schedule is valid. If invalid, falls back
 * to safe default ("Monday-Friday") and logs a warning.
 */
static void validateReadingSchedule()
{
  if (!isValidReadingSchedule(readingSchedule))
  {
    Serial.println("[WARNING] Invalid reading schedule, falling back to Monday-Friday");
    strncpy(readingSchedule, "Monday-Friday", sizeof(readingSchedule) - 1);
    readingSchedule[sizeof(readingSchedule) - 1] = '\0';
  }
}

/**
 * @brief Update cached schedule times based on a local (UTC+offset) time
 */
static void updateResolvedScheduleFromLocal(int hourLocal, int minuteLocal)
{
  int normalizedHour = constrain(hourLocal, 0, 23);
  int normalizedMinute = constrain(minuteLocal, 0, 59);

  g_readHourLocal = normalizedHour;
  g_readMinuteLocal = normalizedMinute;

  int totalLocalMin = normalizedHour * 60 + normalizedMinute;
  int utcMin = (totalLocalMin - TIMEZONE_OFFSET_MINUTES) % (24 * 60);
  if (utcMin < 0)
    utcMin += 24 * 60;
  g_readHourUtc = utcMin / 60;
  g_readMinuteUtc = utcMin % 60;
}

/**
 * @brief Update cached schedule times based on a UTC time
 */
static void updateResolvedScheduleFromUtc(int hourUtc, int minuteUtc)
{
  int normalizedHour = constrain(hourUtc, 0, 23);
  int normalizedMinute = constrain(minuteUtc, 0, 59);

  g_readHourUtc = normalizedHour;
  g_readMinuteUtc = normalizedMinute;

  int totalUtcMin = normalizedHour * 60 + normalizedMinute;
  int localMin = (totalUtcMin + TIMEZONE_OFFSET_MINUTES) % (24 * 60);
  if (localMin < 0)
    localMin += 24 * 60;
  g_readHourLocal = localMin / 60;
  g_readMinuteLocal = localMin % 60;
}

/**
 * @brief Check if today is a scheduled reading day
 *
 * Evaluates whether the current day (based on tm structure) falls within
 * the configured reading schedule.
 *
 * @param ptm Pointer to tm structure with current date/time
 * @return true if today is a reading day according to schedule
 */
bool isReadingDay(struct tm *ptm)
{
  // Delegates to ScheduleManager module
  return ScheduleManager::isReadingDay(ptm);
}

// ============================================================================
// Signal Quality Conversion API
// ============================================================================

/**
 * @brief Convert WiFi RSSI to percentage
 *
 * Maps WiFi RSSI value to 0-100% scale for user-friendly display.
 * Clamps input to reasonable range (-100 to -50 dBm).
 *
 * @param rssi WiFi RSSI in dBm (typically -100 to -50)
 * @return Signal strength as percentage (0-100)
 */
int calculateWiFiSignalStrengthPercentage(int rssi)
{
  int strength = constrain(rssi, -100, -50); // Clamp RSSI to a reasonable range
  return map(strength, -100, -50, 0, 100);   // Map RSSI to percentage (0-100%)
}

/**
 * @brief Convert 433 MHz meter RSSI to percentage
 *
 * Converts CC1101 RSSI measurement (in dBm) to 0-100% scale.
 * Uses wider range (-120 to -40 dBm) appropriate for sub-GHz band.
 *
 * @param rssi_dbm Meter RSSI in dBm (typically -120 to -40)
 * @return Signal strength as percentage (0-100)
 */
int calculateMeterdBmToPercentage(int rssi_dbm)
{
  // Clamp RSSI to a reasonable range (e.g., -120 dBm to -40 dBm)
  int clamped_rssi = constrain(rssi_dbm, -120, -40);

  // Map the clamped RSSI value to a percentage (0-100%)
  return map(clamped_rssi, -120, -40, 0, 100);
}

/**
 * @brief Convert LQI to percentage
 *
 * Converts CC1101 Link Quality Indicator (0-255) to 0-100% scale.
 * LQI represents overall link quality including interference effects.
 *
 * @param lqi Link Quality Indicator (0-255, higher is better)
 * @return Link quality as percentage (0-100)
 */
int calculateLQIToPercentage(int lqi)
{
  int strength = constrain(lqi, 0, 255); // Clamp LQI to valid range
  return map(strength, 0, 255, 0, 100);  // Map LQI to percentage
}

// ------------------------------
// Connectivity watchdog state
// ------------------------------
static unsigned long g_bootMillis = 0;
static unsigned long g_wifiAttemptStartMs = 0;
static unsigned long g_mqttAttemptStartMs = 0;
static unsigned long g_wifiOfflineSince = 0;
static unsigned long g_mqttOfflineSince = 0;
static unsigned long g_lastConnLogMs = 0;
static unsigned long g_lastLedBlinkMs = 0;
static bool g_ledState = false;
static bool g_prevWifiUp = false;
static bool g_prevMqttUp = false;

// Helper: translate Wi-Fi wl_status_t to readable string
static const char *wifiStatusToString(wl_status_t st)
{
  switch (st)
  {
  case WL_IDLE_STATUS:
    return "IDLE (starting up)";
  case WL_NO_SSID_AVAIL:
    return "NO_SSID_AVAIL (SSID not found)";
  case WL_SCAN_COMPLETED:
    return "SCAN_COMPLETED";
  case WL_CONNECTED:
    return "CONNECTED";
  case WL_CONNECT_FAILED:
    return "CONNECT_FAILED";
  case WL_CONNECTION_LOST:
    return "CONNECTION_LOST";
  case WL_DISCONNECTED:
    return "DISCONNECTED";
#ifdef WL_WRONG_PASSWORD
  case WL_WRONG_PASSWORD:
    return "WRONG_PASSWORD";
#endif
  default:
    return "UNKNOWN";
  }
}

// Function: onUpdateData
// Description: Fetches data from the water and gas meter and publishes it to MQTT topics.
//              Retries up to 10 times if data retrieval fails.
void onUpdateData()
{
  Serial.printf("[STATUS] Firmware version: %s\n", EVERBLU_FW_VERSION);
  Serial.printf("[STATUS] Updating data from meter...\n");
  Serial.printf("[STATUS] Retry count: %d\n", _retry);
  Serial.printf("[STATUS] Reading schedule: %s\n", readingSchedule);
  Serial.printf("[STATUS] Scheduled read time: %02d:%02d UTC (%02d:%02d local-offset)\n", g_readHourUtc, g_readMinuteUtc, g_readHourLocal, g_readMinuteLocal);

  // Increment total attempts counter
  totalReadAttempts++;

  // Indicate activity with LED
  digitalWrite(LED_BUILTIN, LOW); // Turn on LED to indicate activity

  // Notify MQTT that active reading has started
  mqtt.publish(String(mqttBaseTopic) + "/active_reading", "true", true);
  mqtt.publish(String(mqttBaseTopic) + "/cc1101_state", "Reading", true);

  struct tmeter_data meter_data = get_meter_data(); // Fetch meter data

  // Get current UTC time
  time_t tnow = time(nullptr);
  struct tm *ptm = gmtime(&tnow);
  Serial.printf("[TIME] Current date (UTC): %04d/%02d/%02d %02d:%02d/%02d - %ld\n", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec, (long)tnow);

  char iso8601[128];
  strftime(iso8601, sizeof iso8601, "%FT%TZ", gmtime(&tnow));

  // Handle data retrieval failure (including first-layer protection rejecting
  // corrupted frames and returning zeros).
  if (meter_data.reads_counter == 0 || meter_data.volume == 0)
  {
    Serial.printf("[ERROR] Unable to retrieve data from meter (attempt %d/%d)\n", _retry + 1, max_retries);

    if (_retry < max_retries - 1)
    {
      // Schedule retry using callback instead of recursion to prevent stack overflow
      _retry++;
      static char errorMsg[64];
      snprintf(errorMsg, sizeof(errorMsg), "Retry %d/%d - No data received", _retry, max_retries);
      lastErrorMessage = errorMsg;
      Serial.printf("[STATUS] Scheduling retry in 10 seconds... (next attempt %d/%d)\n", _retry + 1, max_retries);
      mqtt.publish(String(mqttBaseTopic) + "/active_reading", "false", true);
      mqtt.publish(String(mqttBaseTopic) + "/cc1101_state", cc1101RadioConnected ? "Idle" : "unavailable", true);
      mqtt.publish(String(mqttBaseTopic) + "/last_error", lastErrorMessage, true);
      digitalWrite(LED_BUILTIN, HIGH); // Turn off LED
      // Use non-blocking callback instead of recursive call
      mqtt.executeDelayed(10000, onUpdateData);
    }
    else
    {
      // Max retries reached, enter cooldown period
      lastFailedAttempt = millis();
      failedReads++;
      lastErrorMessage = "Max retries reached - cooling down";
      Serial.printf("[ERROR] Max retries (%d) reached. Entering 1-hour cooldown period.\n", max_retries);
      mqtt.publish(String(mqttBaseTopic) + "/active_reading", "false", true);
      mqtt.publish(String(mqttBaseTopic) + "/cc1101_state", cc1101RadioConnected ? "Idle" : "unavailable", true);
      mqtt.publish(String(mqttBaseTopic) + "/status_message", "Failed after max retries, cooling down for 1 hour", true);
      mqtt.publish(String(mqttBaseTopic) + "/last_error", lastErrorMessage, true);

      char buffer[16];
      snprintf(buffer, sizeof(buffer), "%lu", failedReads);
      mqtt.publish(String(mqttBaseTopic) + "/failed_reads", buffer, true);

      snprintf(buffer, sizeof(buffer), "%lu", totalReadAttempts);
      mqtt.publish(String(mqttBaseTopic) + "/total_attempts", buffer, true);
      digitalWrite(LED_BUILTIN, HIGH); // Turn off LED
      _retry = 0;                      // Reset retry counter for next scheduled attempt
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

  Serial.println("\n=== METER DATA ===");
  if (meterIsGas)
  {
    Serial.printf("[METER DATA] %-25s: %.3f\n", meterUnit, meter_data.volume / (float)GAS_VOLUME_DIVISOR);
  }
  else
  {
    Serial.printf("[METER DATA] %-25s: %d\n", meterUnit, meter_data.volume);
  }
  Serial.printf("[METER DATA] %-25s: %d\n", "Battery (months)", meter_data.battery_left);
  Serial.printf("[METER DATA] %-25s: %d\n", "Counter", meter_data.reads_counter);
  Serial.printf("[METER DATA] %-25s: %d\n", "RSSI (raw)", meter_data.rssi);
  Serial.printf("[METER DATA] %-25s: %d dBm\n", "RSSI", meter_data.rssi_dbm);
  Serial.printf("[METER DATA] %-25s: %d%%\n", "RSSI (percentage)", calculateMeterdBmToPercentage(meter_data.rssi_dbm));
  Serial.printf("[METER DATA] %-25s: %d\n", "Signal quality (LQI)", meter_data.lqi);
  Serial.printf("[METER DATA] %-25s: %d%%\n", "LQI (percentage)", calculateLQIToPercentage(meter_data.lqi));
  Serial.printf("[METER DATA] %-25s: %s\n", "Time window start", timeStartFormatted);
  Serial.printf("[METER DATA] %-25s: %s\n", "Time window end", timeEndFormatted);
  Serial.println("==================\n");

  // Publish meter data to MQTT (using char buffers instead of String)
  // NOTE: meter_data.volume is the raw counter value from the meter (liters for water;
  // for gas, this raw counter is converted to cubic meters using GAS_VOLUME_DIVISOR).
  char valueBuffer[32];

  if (meterIsGas)
  {
    // Gas meters: publish value in m³ (volume / GAS_VOLUME_DIVISOR)
    // Default divisor 100 = 0.01 m³ per unit (typical EverBlu Cyble gas module)
    float cubicMeters = meter_data.volume / (float)GAS_VOLUME_DIVISOR;
    snprintf(valueBuffer, sizeof(valueBuffer), "%.3f", cubicMeters);
  }
  else
  {
    // Water meters: publish value in liters
    snprintf(valueBuffer, sizeof(valueBuffer), "%d", meter_data.volume);
  }
  mqtt.publish(String(mqttBaseTopic) + "/liters", valueBuffer, true);
  delay(5);

  // Publish historical data as JSON attributes for Home Assistant
  // This provides 13 months of historical volume readings that can be accessed as attributes
  // in Home Assistant. Each value represents the total volume at the end of that month.
  // The meter stores these internally with timestamps, but the RADIAN protocol only
  // returns the volume values without dates.
  int num_history = 0;
  if (meter_data.history_available)
  {
    num_history = MeterHistory::countValidMonths(meter_data.history);

    // If the history block was marked available but contained no non-zero entries, treat as unavailable
    if (num_history == 0)
    {
      Serial.println("[WARN] history_available=true but no non-zero history entries found - skipping history publish for this frame");
      meter_data.history_available = false;
    }
  }

  if (meter_data.history_available)
  {
    // Print history and monthly usage to serial
    MeterHistory::printToSerial(meter_data.history, meter_data.volume, "[HISTORY]");

    // Generate JSON representation
    char historyJson[1024];
    int jsonSize = MeterHistory::generateHistoryJson(meter_data.history, meter_data.volume, historyJson, sizeof(historyJson));

    if (jsonSize > 0)
    {
      Serial.printf("Publishing JSON attributes (%d bytes): %s\n\n", strlen(historyJson), historyJson);
      mqtt.publish(String(mqttBaseTopic) + "/liters_attributes", historyJson, true);
      delay(5);

      // Calculate current month usage for log message
      HistoryStats stats = MeterHistory::calculateStats(meter_data.history, meter_data.volume);
      Serial.printf("> Published %d months historical data (current month usage: %u L)\n",
                    stats.monthCount, stats.currentMonthUsage);
    }
    else
    {
      Serial.println("ERROR: Failed to generate history JSON");
    }
  }

skip_history_publish:;

  snprintf(valueBuffer, sizeof(valueBuffer), "%d", meter_data.reads_counter);
  mqtt.publish(String(mqttBaseTopic) + "/counter", valueBuffer, true);
  delay(5);

  snprintf(valueBuffer, sizeof(valueBuffer), "%d", meter_data.battery_left);
  mqtt.publish(String(mqttBaseTopic) + "/battery", valueBuffer, true);
  delay(5);

  snprintf(valueBuffer, sizeof(valueBuffer), "%d", meter_data.rssi_dbm);
  mqtt.publish(String(mqttBaseTopic) + "/rssi_dbm", valueBuffer, true);
  delay(5);

  snprintf(valueBuffer, sizeof(valueBuffer), "%d", calculateMeterdBmToPercentage(meter_data.rssi_dbm));
  mqtt.publish(String(mqttBaseTopic) + "/rssi_percentage", valueBuffer, true);
  delay(5);

  snprintf(valueBuffer, sizeof(valueBuffer), "%d", meter_data.lqi);
  mqtt.publish(String(mqttBaseTopic) + "/lqi", valueBuffer, true);
  delay(5);
  mqtt.publish(String(mqttBaseTopic) + "/time_start", timeStartFormatted, true);
  delay(5);
  mqtt.publish(String(mqttBaseTopic) + "/time_end", timeEndFormatted, true);
  delay(5);
  mqtt.publish(String(mqttBaseTopic) + "/timestamp", iso8601, true); // timestamp since epoch in UTC
  delay(5);

  snprintf(valueBuffer, sizeof(valueBuffer), "%d", calculateLQIToPercentage(meter_data.lqi));
  mqtt.publish(String(mqttBaseTopic) + "/lqi_percentage", valueBuffer, true);
  delay(5);

  // Publish all data as a JSON message as well this is redundant but may be useful for some
  char json[512];
  sprintf(json, jsonTemplate, meter_data.volume, meter_data.reads_counter, meter_data.battery_left, meter_data.rssi, iso8601);
  mqtt.publish(String(mqttBaseTopic) + "/json", json, true);
  delay(5);

#if AUTO_ALIGN_READING_TIME
  // Optionally auto-align the daily scheduled reading time to the meter's wake window
  // Only apply when this read was triggered by the scheduler, not by manual MQTT command
  if (g_isScheduledRead)
  {
    int timeStart = constrain(meter_data.time_start, 0, 23);
    int timeEnd = constrain(meter_data.time_end, 0, 23);
    int window = (timeEnd - timeStart + 24) % 24; // hours in window (0 means unknown/all-day)

    if (window > 0)
    {
#if AUTO_ALIGN_USE_MIDPOINT
      int alignedHourLocal = (timeStart + (window / 2)) % 24; // midpoint (interpreted as local-offset time)
#else
      int alignedHourLocal = timeStart; // start of window (local-offset time)
#endif
      int alignedMinuteLocal = g_readMinuteLocal;
      updateResolvedScheduleFromLocal(alignedHourLocal, alignedMinuteLocal);

      // Publish updated reading_time HH:MM
      char readingTimeFormatted2[6];
      snprintf(readingTimeFormatted2, sizeof(readingTimeFormatted2), "%02d:%02d", g_readHourUtc, g_readMinuteUtc);
      mqtt.publish(String(mqttBaseTopic) + "/reading_time", readingTimeFormatted2, true);
      delay(5);

      Serial.printf("> Auto-aligned reading time to %02d:%02d local-offset (%02d:%02d UTC) (window %02d-%02d local)\n",
                    g_readHourLocal, g_readMinuteLocal, g_readHourUtc, g_readMinuteUtc, timeStart, timeEnd);
    }
  }
#endif

  // Notify MQTT that active reading has ended
  mqtt.publish(String(mqttBaseTopic) + "/active_reading", "false", true);
  mqtt.publish(String(mqttBaseTopic) + "/cc1101_state", cc1101RadioConnected ? "Idle" : "unavailable", true);
  digitalWrite(LED_BUILTIN, HIGH); // Turn off LED to indicate completion

  // Reset retry counter and cooldown on successful read
  _retry = 0;
  lastFailedAttempt = 0;
  successfulReads++;
  lastErrorMessage = "None";

  // Publish success metrics (using char buffers instead of String)
  char metricBuffer[16];

  snprintf(metricBuffer, sizeof(metricBuffer), "%lu", successfulReads);
  mqtt.publish(String(mqttBaseTopic) + "/successful_reads", metricBuffer, true);

  snprintf(metricBuffer, sizeof(metricBuffer), "%lu", totalReadAttempts);
  mqtt.publish(String(mqttBaseTopic) + "/total_attempts", metricBuffer, true);

  mqtt.publish(String(mqttBaseTopic) + "/last_error", "None", true);

  // Perform adaptive frequency tracking based on FREQEST register
  FrequencyManager::adaptiveFrequencyTracking(meter_data.freqest);

  // Reset scheduled read flag for next invocation
  g_isScheduledRead = false;

  Serial.println("[STATUS] Data update complete.\n");
}

// Function: onScheduled
// Description: Schedules daily meter readings at the configured local-offset time.
// If NTP time is not available, this function will pause automatic scheduling
// but manual requests via MQTT will still be processed.
void onScheduled()
{
  // If NTP is not synchronized, skip automatic scheduling
  if (!g_ntpSynced)
  {
    Serial.println("[SCHEDULER] NTP time not available - automatic scheduling paused. Manual requests via MQTT are still available.");
    // Check for NTP sync every 30 seconds
    mqtt.executeDelayed(30000, onScheduled);
    return;
  }

  time_t tnow = time(nullptr);
  // Compute local-offset time by adding offset minutes to UTC epoch
  time_t tlocal = tnow + (time_t)TIMEZONE_OFFSET_MINUTES * 60;
  struct tm *ptm = gmtime(&tlocal);

  // Check if today is a valid reading day
  const bool timeMatch = (ptm->tm_hour == g_readHourLocal && ptm->tm_min == g_readMinuteLocal);

  if (isReadingDay(ptm) && timeMatch && ptm->tm_sec == 0)
  {
    // Check if we're still in cooldown period after failed attempts
    if (lastFailedAttempt > 0 && (millis() - lastFailedAttempt) < RETRY_COOLDOWN)
    {
      unsigned long remainingCooldown = (RETRY_COOLDOWN - (millis() - lastFailedAttempt)) / 1000;
      Serial.printf("Still in cooldown period. %lu seconds remaining.\n", remainingCooldown);

      char cooldownMsg[64];
      snprintf(cooldownMsg, sizeof(cooldownMsg), "Cooldown active, %lus remaining", remainingCooldown);
      char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
      snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
      mqtt.publish(topicBuffer, cooldownMsg, true);
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
    g_isScheduledRead = true; // Mark this read as triggered by scheduler
    onUpdateData();
    return;
  }

  // Check every 500 ms
  mqtt.executeDelayed(500, onScheduled);
}

// ============================================================================
// Home Assistant MQTT Discovery Helper Functions
// ============================================================================
// Supported abbreviations in MQTT discovery messages for Home Assistant
// Used to reduce the size of the JSON payload
// https://www.home-assistant.io/integrations/mqtt/#supported-abbreviations-in-mqtt-discovery-messages

// Helper function to build JSON device block with METER_SERIAL
String buildDeviceJson()
{
  // Pre-allocate memory to prevent heap fragmentation
  // Estimated size: ~250 bytes for device JSON
  String json;
  json.reserve(300);

  json = "\"ids\": [\"" + String(METER_SERIAL) + "\"],\n";
  json += "    \"name\": \"EverBlu Meter " + String(METER_SERIAL) + "\",\n";
  json += "    \"mdl\": \"Itron EverBlu Cyble Enhanced Water and Gas Meter ESP8266/ESP32\",\n";
  json += "    \"mf\": \"Genestealer\",\n";
  json += "    \"sw\": \"" EVERBLU_FW_VERSION "\",\n";
  json += "    \"cu\": \"https://github.com/genestealer/everblu-meters-esp8266-improved\"";
  return json;
}

// Helper function to build discovery JSON for a sensor
String buildDiscoveryJson(const char *name, const char *entity_id, const char *icon,
                          const char *unit = nullptr, const char *dev_class = nullptr,
                          const char *state_class = nullptr, const char *ent_category = nullptr)
{
  // Pre-allocate memory to prevent heap fragmentation from multiple reallocations
  // Estimated size: base structure (~350 bytes) + optional fields (~100 bytes)
  String json;
  json.reserve(512);

  json = "{\n";
  json += "  \"name\": \"" + String(name) + "\",\n";
  json += "  \"uniq_id\": \"" + String(METER_SERIAL) + "_" + String(entity_id) + "\",\n";
  json += "  \"obj_id\": \"" + String(METER_SERIAL) + "_" + String(entity_id) + "\",\n";
  if (icon)
    json += "  \"ic\": \"" + String(icon) + "\",\n";
  if (unit)
    json += "  \"unit_of_meas\": \"" + String(unit) + "\",\n";
  if (dev_class)
    json += "  \"dev_cla\": \"" + String(dev_class) + "\",\n";
  if (state_class)
    json += "  \"stat_cla\": \"" + String(state_class) + "\",\n";
  json += "  \"qos\": 0,\n";
  json += "  \"avty_t\": \"" + String(mqttBaseTopic) + "/status\",\n";
  json += "  \"stat_t\": \"" + String(mqttBaseTopic) + "/" + String(entity_id) + "\",\n";
  json += "  \"frc_upd\": true,\n";
  if (ent_category)
    json += "  \"ent_cat\": \"" + String(ent_category) + "\",\n";
  json += "  \"dev\": {\n    " + buildDeviceJson() + "\n  }\n";
  json += "}";
  return json;
}

// NOTE: Old PROGMEM JSON strings removed - now using dynamic publishHADiscovery() function
// This allows entity IDs and MQTT topics to include METER_SERIAL for multi-meter support

// Function: publishWifiDetails
// Description: Publishes Wi-Fi diagnostics (IP, RSSI, signal strength, etc.) to MQTT.
void publishWifiDetails()
{
  Serial.println("> Publish Wi-Fi details...");

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

  const char *status = (WiFi.status() == WL_CONNECTED) ? "online" : "offline";

  // Uptime calculation
  unsigned long uptimeMillis = millis();
  time_t uptimeSeconds = uptimeMillis / 1000;
  time_t now = time(nullptr);
  time_t uptimeTimestamp = now - uptimeSeconds;
  char uptimeISO[32];
  strftime(uptimeISO, sizeof(uptimeISO), "%FT%TZ", gmtime(&uptimeTimestamp));

  // Publish diagnostic sensors (using char buffers instead of String)
  char valueBuffer[16];
  char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];

  snprintf(topicBuffer, sizeof(topicBuffer), "%s/wifi_ip", mqttBaseTopic);
  mqtt.publish(topicBuffer, wifiIP, true);
  delay(5);

  snprintf(valueBuffer, sizeof(valueBuffer), "%d", wifiRSSI);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/wifi_rssi", mqttBaseTopic);
  mqtt.publish(topicBuffer, valueBuffer, true);
  delay(5);

  snprintf(valueBuffer, sizeof(valueBuffer), "%d", wifiSignalPercentage);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/wifi_signal_percentage", mqttBaseTopic);
  mqtt.publish(topicBuffer, valueBuffer, true);
  delay(5);

  snprintf(topicBuffer, sizeof(topicBuffer), "%s/mac_address", mqttBaseTopic);
  mqtt.publish(topicBuffer, macAddress, true);
  delay(5);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/wifi_ssid", mqttBaseTopic);
  mqtt.publish(topicBuffer, wifiSSID, true);
  delay(5);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/wifi_bssid", mqttBaseTopic);
  mqtt.publish(topicBuffer, wifiBSSID, true);
  delay(5);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/status", mqttBaseTopic);
  mqtt.publish(topicBuffer, status, true);
  delay(5);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/uptime", mqttBaseTopic);
  mqtt.publish(topicBuffer, uptimeISO, true);
  delay(5);

  Serial.println("> Wi-Fi details published");
}

// Function: publishMeterSettings
// Description: Publishes meter configuration (year, serial, frequency) to MQTT.
void publishMeterSettings()
{
  Serial.println("> Publish meter settings...");

  // Publish Meter Year, Serial (using char buffers instead of String)
  char valueBuffer[16];
  char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];

  snprintf(valueBuffer, sizeof(valueBuffer), "%d", METER_YEAR);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/everblu_meter_year", mqttBaseTopic);
  mqtt.publish(topicBuffer, valueBuffer, true);
  delay(5);

  snprintf(valueBuffer, sizeof(valueBuffer), "%u", METER_SERIAL);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/everblu_meter_serial", mqttBaseTopic);
  mqtt.publish(topicBuffer, valueBuffer, true);
  delay(5);

  // Publish Reading Schedule
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/reading_schedule", mqttBaseTopic);
  mqtt.publish(topicBuffer, readingSchedule, true);
  delay(5);

  // Publish Reading Time (UTC) as HH:MM text (resolved time that may be auto-aligned)
  char readingTimeFormatted[6];
  snprintf(readingTimeFormatted, sizeof(readingTimeFormatted), "%02d:%02d", (int)g_readHourUtc, (int)g_readMinuteUtc);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/reading_time", mqttBaseTopic);
  mqtt.publish(topicBuffer, readingTimeFormatted, true);
  delay(5);

  Serial.println("> Meter settings published");
}

// Function: publishDiscoveryMessage
// Description: Helper function to publish a single MQTT discovery message for Home Assistant
// @param domain The Home Assistant domain (sensor, button, binary_sensor, etc.)
// @param entity The entity name suffix (e.g., "everblu_meter_value")
// @param jsonPayload The complete JSON discovery payload
static void publishDiscoveryMessage(const char *domain, const char *entity, const String &jsonPayload)
{
  // Buffer sizes increased for safety margin to prevent overflow
  // Worst case: "homeassistant/" (14) + "binary_sensor/" (14) +
  // METER_SERIAL (10) + "_" (1) + entity (50) + "/config" (7) + null (1) = ~97 bytes
  char configTopic[256];
  char entityId[128];
  snprintf(entityId, sizeof(entityId), "%lu_%s", (unsigned long)METER_SERIAL, entity);
  snprintf(configTopic, sizeof(configTopic), "homeassistant/%s/%s/config", domain, entityId);
  mqtt.publish(configTopic, jsonPayload.c_str(), true);
  delay(5);
}

// Function: publishHADiscovery
// Description: Publishes all Home Assistant MQTT discovery messages with serial-specific entity IDs
void publishHADiscovery()
{
  Serial.println("> Publishing Home Assistant discovery messages...");

  String json;

  // Reading (Total) - Main water/gas sensor
  Serial.println("> Publishing Reading (Total) sensor discovery...");
  json = "{\n";
  json += "  \"name\": \"Reading (Total)\",\n";
  json += "  \"uniq_id\": \"" + String(METER_SERIAL) + "_everblu_meter_value\",\n";
  json += "  \"obj_id\": \"" + String(METER_SERIAL) + "_everblu_meter_value\",\n";
  json += "  \"ic\": \"" + String(meterIcon) + "\",\n";
  json += "  \"unit_of_meas\": \"" + String(meterUnit) + "\",\n";
  json += "  \"dev_cla\": \"" + String(meterDeviceClass) + "\",\n";
  json += "  \"stat_cla\": \"total_increasing\",\n";
  json += "  \"qos\": 0,\n";
  json += "  \"avty_t\": \"" + String(mqttBaseTopic) + "/status\",\n";
  json += "  \"stat_t\": \"" + String(mqttBaseTopic) + "/liters\",\n";
  Serial.printf("> Water Usage state topic: %s/liters\n", mqttBaseTopic);
  json += "  \"json_attr_t\": \"" + String(mqttBaseTopic) + "/liters_attributes\",\n";
  json += "  \"sug_dsp_prc\": 0,\n";
  json += "  \"frc_upd\": true,\n";
  json += "  \"dev\": {\n    " + buildDeviceJson() + "\n  }\n";
  json += "}";
  publishDiscoveryMessage("sensor", "everblu_meter_value", json);

  // Read Counter
  json = "{\n";
  json += "  \"name\": \"Read Counter\",\n";
  json += "  \"uniq_id\": \"" + String(METER_SERIAL) + "_everblu_meter_counter\",\n";
  json += "  \"obj_id\": \"" + String(METER_SERIAL) + "_everblu_meter_counter\",\n";
  json += "  \"ic\": \"mdi:counter\",\n";
  json += "  \"qos\": 0,\n";
  json += "  \"avty_t\": \"" + String(mqttBaseTopic) + "/status\",\n";
  json += "  \"stat_t\": \"" + String(mqttBaseTopic) + "/counter\",\n";
  json += "  \"frc_upd\": true,\n";
  json += "  \"dev\": {\n    " + buildDeviceJson() + "\n  }\n";
  json += "}";
  publishDiscoveryMessage("sensor", "everblu_meter_counter", json);

  // Last Read (timestamp)
  json = "{\n";
  json += "  \"name\": \"Last Read\",\n";
  json += "  \"uniq_id\": \"" + String(METER_SERIAL) + "_everblu_meter_timestamp\",\n";
  json += "  \"obj_id\": \"" + String(METER_SERIAL) + "_everblu_meter_timestamp\",\n";
  json += "  \"ic\": \"mdi:clock\",\n";
  json += "  \"dev_cla\": \"timestamp\",\n";
  json += "  \"qos\": 0,\n";
  json += "  \"avty_t\": \"" + String(mqttBaseTopic) + "/status\",\n";
  json += "  \"stat_t\": \"" + String(mqttBaseTopic) + "/timestamp\",\n";
  json += "  \"frc_upd\": true,\n";
  json += "  \"dev\": {\n    " + buildDeviceJson() + "\n  }\n";
  json += "}";
  publishDiscoveryMessage("sensor", "everblu_meter_timestamp", json);

  // Request Reading Button
  json = "{\n";
  json += "  \"name\": \"Request Reading Now\",\n";
  json += "  \"uniq_id\": \"" + String(METER_SERIAL) + "_everblu_meter_request\",\n";
  json += "  \"obj_id\": \"" + String(METER_SERIAL) + "_everblu_meter_request\",\n";
  json += "  \"qos\": 0,\n";
  json += "  \"avty_t\": \"" + String(mqttBaseTopic) + "/status\",\n";
  json += "  \"cmd_t\": \"" + String(mqttBaseTopic) + "/trigger_force\",\n";
  json += "  \"pl_avail\": \"online\",\n";
  json += "  \"pl_not_avail\": \"offline\",\n";
  json += "  \"pl_prs\": \"update\",\n";
  json += "  \"frc_upd\": true,\n";
  json += "  \"dev\": {\n    " + buildDeviceJson() + "\n  }\n";
  json += "}";
  publishDiscoveryMessage("button", "everblu_meter_request", json);

  // Diagnostic sensors
  publishDiscoveryMessage("sensor", "everblu_meter_wifi_ip", buildDiscoveryJson("IP Address", "wifi_ip", "mdi:ip-network-outline", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "everblu_meter_wifi_rssi", buildDiscoveryJson("WiFi RSSI", "wifi_rssi", "mdi:signal-variant", "dBm", "signal_strength", "measurement", "diagnostic"));
  publishDiscoveryMessage("sensor", "everblu_meter_mac_address", buildDiscoveryJson("MAC Address", "mac_address", "mdi:network", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "everblu_meter_wifi_ssid", buildDiscoveryJson("WiFi SSID", "wifi_ssid", "mdi:help-network-outline", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "everblu_meter_wifi_bssid", buildDiscoveryJson("WiFi BSSID", "wifi_bssid", "mdi:access-point-network", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "everblu_meter_uptime", buildDiscoveryJson("Device Uptime", "uptime", nullptr, nullptr, "timestamp", nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "everblu_meter_wifi_signal_percentage", buildDiscoveryJson("WiFi Signal", "wifi_signal_percentage", "mdi:wifi", "%", nullptr, "measurement", "diagnostic"));
  publishDiscoveryMessage("sensor", "everblu_meter_reading_time", buildDiscoveryJson("Reading Time (UTC)", "reading_time", "mdi:clock-outline", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "everblu_meter_reading_schedule", buildDiscoveryJson("Reading Schedule", "reading_schedule", "mdi:calendar-clock", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "everblu_meter_year", buildDiscoveryJson("Meter Year", "everblu_meter_year", "mdi:calendar", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "everblu_meter_serial", buildDiscoveryJson("Meter Serial", "everblu_meter_serial", "mdi:barcode", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "everblu_meter_battery_months", buildDiscoveryJson("Months Remaining", "battery", "mdi:battery-clock", "months", nullptr, "measurement", nullptr));
  publishDiscoveryMessage("sensor", "everblu_meter_rssi_dbm", buildDiscoveryJson("RSSI", "rssi_dbm", "mdi:signal", "dBm", "signal_strength", "measurement", nullptr));
  publishDiscoveryMessage("sensor", "everblu_meter_rssi_percentage", buildDiscoveryJson("Signal", "rssi_percentage", "mdi:signal-cellular-3", "%", nullptr, "measurement", nullptr));
  publishDiscoveryMessage("sensor", "everblu_meter_lqi_percentage", buildDiscoveryJson("Signal Quality (LQI)", "lqi_percentage", "mdi:signal-cellular-outline", "%", nullptr, "measurement", nullptr));
  publishDiscoveryMessage("sensor", "everblu_meter_time_start", buildDiscoveryJson("Wake Time", "time_start", "mdi:clock-start", nullptr, nullptr, nullptr, nullptr));
  publishDiscoveryMessage("sensor", "everblu_meter_time_end", buildDiscoveryJson("Sleep Time", "time_end", "mdi:clock-end", nullptr, nullptr, nullptr, nullptr));
  publishDiscoveryMessage("sensor", "everblu_meter_total_attempts", buildDiscoveryJson("Total Read Attempts", "total_attempts", "mdi:counter", nullptr, nullptr, "total_increasing", "diagnostic"));
  publishDiscoveryMessage("sensor", "everblu_meter_successful_reads", buildDiscoveryJson("Successful Reads", "successful_reads", "mdi:check-circle", nullptr, nullptr, "total_increasing", "diagnostic"));
  publishDiscoveryMessage("sensor", "everblu_meter_failed_reads", buildDiscoveryJson("Failed Reads", "failed_reads", "mdi:alert-circle", nullptr, nullptr, "total_increasing", "diagnostic"));
  publishDiscoveryMessage("sensor", "everblu_meter_last_error", buildDiscoveryJson("Last Error", "last_error", "mdi:alert", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "everblu_meter_cc1101_state", buildDiscoveryJson("CC1101 State", "cc1101_state", "mdi:radio-tower", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "everblu_meter_freq_offset", buildDiscoveryJson("Frequency Offset", "frequency_offset", "mdi:sine-wave", "kHz", nullptr, nullptr, "diagnostic"));

  // Buttons
  json = "{\n";
  json += "  \"name\": \"Restart Device\",\n";
  json += "  \"uniq_id\": \"" + String(METER_SERIAL) + "_everblu_meter_restart\",\n";
  json += "  \"obj_id\": \"" + String(METER_SERIAL) + "_everblu_meter_restart\",\n";
  json += "  \"qos\": 0,\n";
  json += "  \"avty_t\": \"" + String(mqttBaseTopic) + "/status\",\n";
  json += "  \"cmd_t\": \"" + String(mqttBaseTopic) + "/restart\",\n";
  json += "  \"pl_prs\": \"restart\",\n";
  json += "  \"ent_cat\": \"config\",\n";
  json += "  \"dev\": {\n    " + buildDeviceJson() + "\n  }\n";
  json += "}";
  publishDiscoveryMessage("button", "everblu_meter_restart", json);

  json = "{\n";
  json += "  \"name\": \"Scan Frequency\",\n";
  json += "  \"uniq_id\": \"" + String(METER_SERIAL) + "_everblu_meter_freq_scan\",\n";
  json += "  \"obj_id\": \"" + String(METER_SERIAL) + "_everblu_meter_freq_scan\",\n";
  json += "  \"ic\": \"mdi:magnify-scan\",\n";
  json += "  \"qos\": 0,\n";
  json += "  \"avty_t\": \"" + String(mqttBaseTopic) + "/status\",\n";
  json += "  \"cmd_t\": \"" + String(mqttBaseTopic) + "/frequency_scan\",\n";
  json += "  \"pl_prs\": \"scan\",\n";
  json += "  \"ent_cat\": \"config\",\n";
  json += "  \"dev\": {\n    " + buildDeviceJson() + "\n  }\n";
  json += "}";
  publishDiscoveryMessage("button", "everblu_meter_freq_scan", json);

  // Binary sensor for active reading
  json = "{\n";
  json += "  \"name\": \"Active Reading\",\n";
  json += "  \"uniq_id\": \"" + String(METER_SERIAL) + "_everblu_meter_active_reading\",\n";
  json += "  \"obj_id\": \"" + String(METER_SERIAL) + "_everblu_meter_active_reading\",\n";
  json += "  \"dev_cla\": \"running\",\n";
  json += "  \"qos\": 0,\n";
  json += "  \"avty_t\": \"" + String(mqttBaseTopic) + "/status\",\n";
  json += "  \"stat_t\": \"" + String(mqttBaseTopic) + "/active_reading\",\n";
  json += "  \"pl_on\": \"true\",\n";
  json += "  \"pl_off\": \"false\",\n";
  json += "  \"dev\": {\n    " + buildDeviceJson() + "\n  }\n";
  json += "}";
  publishDiscoveryMessage("binary_sensor", "everblu_meter_active_reading", json);

  Serial.println("> Home Assistant discovery messages published");
}

// Function: onConnectionEstablished
// Description: Handles MQTT connection establishment, including Home Assistant discovery and OTA setup.
void onConnectionEstablished()
{
  Serial.println("Connected to MQTT Broker :)");

  Serial.println("> Configure time from NTP server. Please wait...");
  // Note, my VLAN has no WAN/internet, so I am useing Home Assistant Community Add-on: chrony to proxy the time
  configTzTime("UTC0", SECRET_NTP_SERVER);

  // Wait briefly for NTP to set the clock and report status
  const time_t MIN_VALID_EPOCH = 1609459200; // 2021-01-01
  const unsigned long NTP_SYNC_TIMEOUT_MS = 10000;
  bool timeSynced = false;
  unsigned long waitStart = millis();
  while (millis() - waitStart < NTP_SYNC_TIMEOUT_MS)
  {
    time_t probe = time(nullptr);
    if (probe >= MIN_VALID_EPOCH)
    {
      timeSynced = true;
      break;
    }
    delay(200);
  }

  time_t tnow = time(nullptr);
  if (timeSynced)
  {
    g_ntpSynced = true;
    Serial.printf("✓ NTP sync successful after %lu ms\n", (unsigned long)(millis() - waitStart));
    Serial.println("[NTP] Time synchronized - automatic scheduling is now ACTIVE");
  }
  else
  {
    g_ntpSynced = false;
    Serial.printf("WARNING: NTP sync failed within %lu ms. Clock may be unset (epoch=%ld).\n",
                  (unsigned long)(millis() - waitStart), (long)tnow);
    Serial.println("[NTP] Time NOT synchronized - automatic scheduling is PAUSED (manual requests via MQTT still available)");
  }

  struct tm *ptm = gmtime(&tnow);
  Serial.printf("Current date (UTC) : %04d/%02d/%02d %02d:%02d/%02d - %ld\n", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec, (long)tnow);
  // Print simple offset and derived local time for debugging
  int offsetMin = TIMEZONE_OFFSET_MINUTES;
  time_t tlocal = tnow + (time_t)offsetMin * 60;
  struct tm *plocal = gmtime(&tlocal);
  Serial.printf("Configured UTC offset: %+d minutes\n", offsetMin);
  Serial.printf("Current date (UTC+offset): %04d/%02d/%02d %02d:%02d/%02d - %ld\n",
                plocal->tm_year + 1900, plocal->tm_mon + 1, plocal->tm_mday,
                plocal->tm_hour, plocal->tm_min, plocal->tm_sec, (long)tlocal);

  // Initialize schedule caches using validated UTC defaults
  updateResolvedScheduleFromUtc(DEFAULT_READING_HOUR_UTC, DEFAULT_READING_MINUTE_UTC);

  Serial.println("> Configure Arduino OTA flash.");
  ArduinoOTA.onStart([]()
                     {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    }
    else { // U_FS
      type = "filesystem";
    }
    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type); });
  ArduinoOTA.onEnd([]()
                   { Serial.println("\nEnd updating."); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("%u%%\r\n", (progress / (total / 100))); });
  ArduinoOTA.onError([](ota_error_t error)
                     {
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
    } });
  ArduinoOTA.setHostname("EVERBLUREADER");
  ArduinoOTA.begin();
  Serial.println("> Ready");
  Serial.print("> IP address: ");
  Serial.println(WiFi.localIP());

  // Start WiFi serial monitor if enabled in configuration
#if WIFI_SERIAL_MONITOR_ENABLED
  wifiSerialBegin();
  Serial.println("> WiFi Serial Monitor: ENABLED");
#else
  Serial.println("> WiFi Serial Monitor: DISABLED");
#endif

  char triggerTopic[80];
  snprintf(triggerTopic, sizeof(triggerTopic), "%s/trigger", mqttBaseTopic);
  mqtt.subscribe(triggerTopic, [](const String &message)
                 {
    // Input validation: only accept whitelisted commands
    if (message != "update" && message != "read") {
      Serial.printf("WARN: Invalid trigger command '%s' (expected 'update' or 'read')\n", message.c_str());
      char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
      snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
      mqtt.publish(topicBuffer, "Invalid trigger command", true);
      return;
    }
    
    // Check if we're in cooldown period
    if (lastFailedAttempt > 0 && (millis() - lastFailedAttempt) < RETRY_COOLDOWN) {
      unsigned long remainingCooldown = (RETRY_COOLDOWN - (millis() - lastFailedAttempt)) / 1000;
      Serial.printf("Cannot trigger update: Still in cooldown period. %lu seconds remaining.\n", remainingCooldown);
      
      char cooldownMsg[64];
      snprintf(cooldownMsg, sizeof(cooldownMsg), "Cooldown active, %lus remaining", remainingCooldown);
      char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
      snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
      mqtt.publish(topicBuffer, cooldownMsg, true);
      return;
    }

    Serial.printf("Update data from meter from MQTT trigger (command: %s)\n", message.c_str());

    _retry = 0;
    onUpdateData(); });

  // Force trigger: an alternate topic that bypasses the cooldown period.
  // This is intended for Home Assistant buttons that should override cooldown.
  char triggerForceTopic[80];
  snprintf(triggerForceTopic, sizeof(triggerForceTopic), "%s/trigger_force", mqttBaseTopic);
  mqtt.subscribe(triggerForceTopic, [](const String &message)
                 {
    // Input validation: accept same commands as the normal trigger
    if (message != "update" && message != "read") {
      Serial.printf("[WARN] Invalid force-trigger command '%s' (expected 'update' or 'read')\n", message.c_str());
      char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
      snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
      mqtt.publish(topicBuffer, "Invalid trigger command", true);
      return;
    }

    Serial.printf("[STATUS] Force update requested via MQTT (command: %s) - overriding cooldown\n", message.c_str());

    // Immediately attempt to update, ignoring any cooldown state
    _retry = 0;
    onUpdateData(); });

  char restartTopic[80];
  snprintf(restartTopic, sizeof(restartTopic), "%s/restart", mqttBaseTopic);
  mqtt.subscribe(restartTopic, [](const String &message)
                 {
                   // Input validation: only accept exact "restart" command
                   if (message != "restart")
                   {
                     Serial.printf("WARN: Invalid restart command '%s' (expected 'restart')\n", message.c_str());
                     char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
                     snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
                     mqtt.publish(topicBuffer, "Invalid restart command", true);
                     return;
                   }

                   Serial.println("Restart command received via MQTT. Restarting in 2 seconds...");
                   char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
                   snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
                   mqtt.publish(topicBuffer, "Device restarting...", true);
                   delay(2000);   // Give time for MQTT message to be sent
                   ESP.restart(); // Restart the ESP device
                 });

  char freqScanTopic[80];
  snprintf(freqScanTopic, sizeof(freqScanTopic), "%s/frequency_scan", mqttBaseTopic);
  mqtt.subscribe(freqScanTopic, [](const String &message)
                 {
    // Input validation: only accept "scan" command
    if (message != "scan") {
      Serial.printf("WARN: Invalid frequency scan command '%s' (expected 'scan')\n", message.c_str());
      char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
      snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
      mqtt.publish(topicBuffer, "Invalid scan command", true);
      return;
    }
    
    Serial.println("Frequency scan command received via MQTT");
    
    // Define callback for status updates
    auto statusCallback = [](const char *state, const char *message) {
      char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
      snprintf(topicBuffer, sizeof(topicBuffer), "%s/cc1101_state", mqttBaseTopic);
      mqtt.publish(topicBuffer, state, true);
      snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
      mqtt.publish(topicBuffer, message, true);
    };
    
    FrequencyManager::performFrequencyScan(statusCallback); });

  Serial.println("> Send MQTT config for HA.");

  // Publish all Home Assistant discovery messages with serial-specific entity IDs
  publishHADiscovery();

  // Set initial state for active reading
  char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/active_reading", mqttBaseTopic);
  mqtt.publish(topicBuffer, "false", true);
  delay(5);

  // Publish CC1101 radio availability status for button enable/disable
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/cc1101_availability", mqttBaseTopic);
  mqtt.publish(topicBuffer, cc1101RadioConnected ? "online" : "offline", true);
  delay(5);

  // Publish initial diagnostic metrics (using char buffers instead of String)
  char metricBuffer[16];

  snprintf(topicBuffer, sizeof(topicBuffer), "%s/cc1101_state", mqttBaseTopic);
  mqtt.publish(topicBuffer, cc1101RadioConnected ? "Idle" : "unavailable", true);
  delay(5);

  snprintf(metricBuffer, sizeof(metricBuffer), "%lu", totalReadAttempts);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/total_attempts", mqttBaseTopic);
  mqtt.publish(topicBuffer, metricBuffer, true);
  delay(5);

  snprintf(metricBuffer, sizeof(metricBuffer), "%lu", successfulReads);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/successful_reads", mqttBaseTopic);
  mqtt.publish(topicBuffer, metricBuffer, true);
  delay(5);

  snprintf(metricBuffer, sizeof(metricBuffer), "%lu", failedReads);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/failed_reads", mqttBaseTopic);
  mqtt.publish(topicBuffer, metricBuffer, true);
  delay(5);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/last_error", mqttBaseTopic);
  mqtt.publish(topicBuffer, lastErrorMessage, true);
  delay(5);

  char freqBuffer[16];
  snprintf(freqBuffer, sizeof(freqBuffer), "%.3f", FrequencyManager::getOffset() * 1000.0); // Convert MHz to kHz
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/frequency_offset", mqttBaseTopic);
  mqtt.publish(topicBuffer, freqBuffer, true);
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

/**
 * @brief Validate configuration parameters
 *
 * Performs startup validation of configuration parameters to fail fast
 * on invalid settings. Checks:
 * - METER_YEAR (must be 0-99)
 * - METER_SERIAL (must be non-zero)
 * - FREQUENCY (must be 300-500 MHz if defined)
 * - READING_HOUR/MINUTE (must be valid time)
 * - Reading schedule string format
 *
 * Logs validation results to serial console with ✓ or ERROR markers.
 *
 * @return true if all validations passed, false if any parameter is invalid
 */
bool validateConfiguration()
{
  bool valid = true;

  Serial.println("\n=== Configuration Validation ===");

  // Validate METER_YEAR (should be 0-99 for years 2000-2099)
  // Common mistake: entering 4-digit year (e.g., 2023 instead of 23)
  if (METER_YEAR > 99)
  {
    if (METER_YEAR >= 2000 && METER_YEAR <= 2099)
    {
      Serial.printf("ERROR: METER_YEAR=%d appears to be a 4-digit year\n", METER_YEAR);
      Serial.printf("       Use 2-digit year format: %d (not %d)\n", METER_YEAR - 2000, METER_YEAR);
      Serial.println("       Example: Serial '23-1875247-234' → use 23");
    }
    else
    {
      Serial.printf("ERROR: Invalid METER_YEAR=%d (expected 0-99)\n", METER_YEAR);
    }
    valid = false;
  }
  else if (METER_YEAR < 10)
  {
    Serial.printf("⚠ METER_YEAR: %d (20%02d) - unusually old meter\n", METER_YEAR, METER_YEAR);
    Serial.printf("  If your serial starts with %02d, this is correct.\n", METER_YEAR);
    Serial.println("  Otherwise, check for missing leading zero in private.h");
  }
  else
  {
    Serial.printf("✓ METER_YEAR: %d (20%02d)\n", METER_YEAR, METER_YEAR);
  }

  // Validate METER_SERIAL (should not be 0, and should be reasonable length)
  // Serial format on meter: XX-YYYYYYY-ZZZ (use only middle part YYYYYYY)
  // Note: If middle part starts with 0, omit leading zeros (e.g., 0123456 → 123456)
  if (METER_SERIAL == 0)
  {
    Serial.println("ERROR: METER_SERIAL not configured (value is 0)");
    Serial.println("       Use the middle part of your meter's serial number");
    Serial.println("       Example: Serial '23-1875247-234' → use 1875247");
    valid = false;
  }
  else if (METER_SERIAL > 99999999UL) // 8 digits max (most serials are 6-7 digits)
  {
    Serial.printf("ERROR: METER_SERIAL=%lu seems too long (>8 digits)\n", (unsigned long)METER_SERIAL);
    Serial.println("       Use only the middle part of the serial (ignore last part)");
    Serial.println("       Example: Serial '23-1875247-234' → use 1875247 (not 1875247234)");
    valid = false;
  }
  else if (METER_SERIAL < 10UL) // Very suspiciously short (< 2 digits)
  {
    Serial.printf("WARNING: METER_SERIAL=%lu is very short (<2 digits)\n", (unsigned long)METER_SERIAL);
    Serial.println("         Double-check you entered the complete middle part");
    Serial.printf("⚠ METER_SERIAL: %lu\n", (unsigned long)METER_SERIAL);
  }
  else if (METER_SERIAL < 1000UL) // Possibly correct if serial had leading zeros
  {
    Serial.printf("✓ METER_SERIAL: %lu (if your serial started with zeros, this is correct)\n", (unsigned long)METER_SERIAL);
  }
  else
  {
    Serial.printf("✓ METER_SERIAL: %lu\n", (unsigned long)METER_SERIAL);
  }

// Validate FREQUENCY if defined (should be 300-500 MHz for 433 MHz band)
#ifdef FREQUENCY
  if (FREQUENCY < 300.0 || FREQUENCY > 500.0)
  {
    Serial.printf("ERROR: Invalid FREQUENCY=%.2f MHz (expected 300-500 MHz)\n", FREQUENCY);
    valid = false;
  }
  else
  {
    Serial.printf("✓ FREQUENCY: %.6f MHz\n", FREQUENCY);
  }
#else
  Serial.println("✓ FREQUENCY: Using default 433.82 MHz (RADIAN protocol)");
#endif

  // Validate reading time defaults (UTC)
  if (DEFAULT_READING_HOUR_UTC < 0 || DEFAULT_READING_HOUR_UTC > 23)
  {
    Serial.printf("ERROR: Invalid DEFAULT_READING_HOUR_UTC=%d (expected 0-23)\n", DEFAULT_READING_HOUR_UTC);
    valid = false;
  }
  else if (DEFAULT_READING_MINUTE_UTC < 0 || DEFAULT_READING_MINUTE_UTC > 59)
  {
    Serial.printf("ERROR: Invalid DEFAULT_READING_MINUTE_UTC=%d (expected 0-59)\n", DEFAULT_READING_MINUTE_UTC);
    valid = false;
  }
  else
  {
    Serial.printf("✓ Reading Time (UTC): %02d:%02d\n", DEFAULT_READING_HOUR_UTC, DEFAULT_READING_MINUTE_UTC);
  }

// Validate GDO0 pin (basic check - should be defined)
#ifdef GDO0
  Serial.printf("✓ GDO0 Pin: GPIO %d\n", GDO0);
#else
  Serial.println("ERROR: GDO0 pin not defined in private.h");
  valid = false;
#endif

  // Validate reading schedule
  if (!isValidReadingSchedule(readingSchedule))
  {
    Serial.printf("WARNING: Invalid reading schedule '%s'. Will fall back to 'Monday-Friday'.\n", readingSchedule);
    Serial.println("         Expected: 'Monday-Friday', 'Monday-Saturday', or 'Monday-Sunday'");
  }
  else
  {
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
  // Give Serial a moment to initialize
  delay(100);

// On platforms with native USB Serial (e.g. some ESP32 cores) wait briefly for host to open the port
#if defined(ESP32)
  unsigned long __serial_wait_start = millis();
  while (!Serial && millis() - __serial_wait_start < 1000)
  {
    delay(10);
  }
#endif
  Serial.println("Everblu Meters ESP8266/ESP32 Starting...");
  Serial.println("Water/Gas usage data for Home Assistant");
  Serial.println("https://github.com/genestealer/everblu-meters-esp8266-improved");
  Serial.printf("Firmware version: %s\n", EVERBLU_FW_VERSION);
  Serial.printf("Target meter: 20%02d-%07lu\n\n", METER_YEAR, (unsigned long)METER_SERIAL);

  // Initialize meter type configuration
  initMeterTypeConfig();

  // Validate configuration before proceeding
  if (!validateConfiguration())
  {
    Serial.println("\n*** FATAL: Configuration validation failed! ***");
    Serial.println("*** Fix the errors in private.h and reflash ***");
    Serial.println("*** Device halted - will not continue ***\n");
    while (1)
    {
      digitalWrite(LED_BUILTIN, LOW); // Blink LED to indicate error
      delay(200);
      digitalWrite(LED_BUILTIN, HIGH);
      delay(200);
    }
  }

  // Initialize resolved schedule caches using UTC defaults and configured timezone offset
  updateResolvedScheduleFromUtc(DEFAULT_READING_HOUR_UTC, DEFAULT_READING_MINUTE_UTC);

  // Initialize global variables that depend on meter serial
  snprintf(meterSerialStr, sizeof(meterSerialStr), "%07lu", (unsigned long)METER_SERIAL);
  snprintf(mqttBaseTopic, sizeof(mqttBaseTopic), "everblu/cyble/%07lu", (unsigned long)METER_SERIAL);
  snprintf(mqttLwtTopic, sizeof(mqttLwtTopic), "%s/status", mqttBaseTopic);
  strncpy(readingSchedule, DEFAULT_READING_SCHEDULE, sizeof(readingSchedule) - 1);
  readingSchedule[sizeof(readingSchedule) - 1] = '\0';

  Serial.println("✓ Configuration valid - proceeding with initialization\n");

  // Note: mqttBaseTopic and meterSerialStr are now initialized above
  Serial.printf("> MQTT base topic: %s\n", mqttBaseTopic);
  Serial.printf("> Meter serial string: %s\n", meterSerialStr);
  Serial.printf("> mqttBaseTopic length: %d\n", strlen(mqttBaseTopic));

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW); // turned on to start with

  // Initialize persistent storage (for MQTT client - FrequencyManager handles its own storage)
#if defined(ESP8266)
  // EEPROM is still initialized for FrequencyManager's storage abstraction layer
  Serial.println("> EEPROM system ready");

  // Clear EEPROM if requested (set CLEAR_EEPROM_ON_BOOT=1 in private.h)
  // Use this when replacing ESP board, CC1101 module, or moving to a different meter
#if CLEAR_EEPROM_ON_BOOT
  Serial.println("> CLEARING EEPROM (CLEAR_EEPROM_ON_BOOT = 1)...");
  StorageAbstraction::clearAll();
  Serial.println("> EEPROM cleared. Remember to set CLEAR_EEPROM_ON_BOOT = 0 after testing!");
#endif
#endif

  // Initialize FrequencyManager and load stored frequency offset
  Serial.println("> Initializing Frequency Manager...");

  // Register callbacks: Inject our CC1101 functions for maximum reusability
  // ESPHome or other projects can inject their own implementations here
  FrequencyManager::setRadioInitCallback(cc1101_init);
  FrequencyManager::setMeterReadCallback(get_meter_data);

  FrequencyManager::begin(FREQUENCY);
  FrequencyManager::setAutoScanEnabled(AUTO_SCAN_ENABLED != 0);

  // If no valid frequency offset found and auto-scan is enabled, perform wide initial scan
  if (FrequencyManager::shouldPerformAutoScan())
  {
    Serial.println("> No stored frequency offset found. Performing wide initial scan...");

    // Define callback for status updates during scan
    auto statusCallback = [](const char *state, const char *message)
    {
      // Status updates will be logged by FrequencyManager
      // MQTT not yet connected, so just log here
      Serial.printf("> Scan status: %s - %s\n", state, message);
    };

    FrequencyManager::performWideInitialScan(statusCallback);
  }
  else if (FrequencyManager::getOffset() == 0.0)
  {
    Serial.println("> AUTO_SCAN_ENABLED=0; skipping automatic frequency scan (offset remains 0.0 MHz).");
  }

  // Increase the max packet size to handle large MQTT payloads
  mqtt.setMaxPacketSize(2048); // Set to a size larger than your longest payload

  // Set the Last Will and Testament (LWT) with serial-specific topic
  // Note: mqttLwtTopic is initialized at global scope to ensure it's ready before MQTT client connects
  mqtt.enableLastWillMessage(mqttLwtTopic, "offline", true); // You can activate the retain flag by setting the third parameter to true

  // Make reconnection attempts faster and deterministic
  mqtt.setWifiReconnectionAttemptDelay(15000); // try every 15s
  mqtt.setMqttReconnectionAttemptDelay(15000); // try every 15s
  // In rare cases where the board wedges during connection attempts, allow drastic reset
  mqtt.enableDrasticResetOnConnectionFailures();

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
  Serial.println("NOTE: FREQUENCY not set in private.h; using default 433.820000 MHz (RADIAN).");
#endif

  // Optional functionalities of EspMQTTClient
#if ENABLE_MQTT_DEBUGGING
  mqtt.enableDebuggingMessages(true); // Enable debugging messages sent to serial output
  Serial.println(">> MQTT debugging enabled");
#endif

  // Set CC1101 radio frequency with automatic calibration
  Serial.println("> Initializing CC1101 radio...");
  float effectiveFrequency = FrequencyManager::getTunedFrequency();
  if (FrequencyManager::getOffset() != 0.0)
  {
    Serial.printf("> Applying stored frequency offset: %.6f MHz (effective: %.6f MHz)\n",
                  FrequencyManager::getOffset(), effectiveFrequency);
  }
  if (!cc1101_init(effectiveFrequency))
  {
    Serial.println("WARNING: CC1101 radio initialization failed!");
    Serial.println("Please check: 1) Wiring connections 2) 3.3V power supply 3) SPI pins");
    Serial.println("Continuing with WiFi/MQTT only - radio functionality will not be available");
    Serial.println("Device will remain accessible via WiFi/MQTT for diagnostics and configuration");
    cc1101RadioConnected = false;
  }
  else
  {
    Serial.println("> CC1101 radio initialized successfully");
    cc1101RadioConnected = true;
  }

  /*
  // Use this piece of code to test
  struct tmeter_data meter_data;
  meter_data = get_meter_data();
  Serial.printf("\nVolume : %d\nBattery (in months) : %d\nCounter : %d\nTime start : %d\nTime end : %d\n\n", meter_data.volume, meter_data.battery_left, meter_data.reads_counter, meter_data.time_start, meter_data.time_end);
  while (42);
  */

  // Initialize connectivity watchdog timers
  g_bootMillis = millis();
  g_wifiAttemptStartMs = g_bootMillis;
  g_mqttAttemptStartMs = 0; // will start once Wi-Fi is up
  g_wifiOfflineSince = 0;
  g_mqttOfflineSince = 0;
  g_lastConnLogMs = 0;
  g_lastLedBlinkMs = millis();

  Serial.println("> Waiting for Wi-Fi/MQTT... timeouts enabled (Wi-Fi 30s, MQTT 30s). Will retry automatically.");
}

// ============================================================================
// Main Loop
// ============================================================================

/**
 * @brief Main loop function
 *
 * Handles MQTT communication, OTA updates, state machine execution,
 * and periodic WiFi diagnostics publishing.
 */
void loop()
{
  mqtt.loop();
  ArduinoOTA.handle();
#if WIFI_SERIAL_MONITOR_ENABLED
  wifiSerialLoop();
#endif

  // Update diagnostics and Wi-Fi details every 5 minutes
  if (millis() - lastWifiUpdate > 300000)
  { // 5 minutes in ms
    publishWifiDetails();
    lastWifiUpdate = millis();
  }

  // ------------------------------
  // Connectivity watchdog & LED feedback
  // ------------------------------
  const bool wifiUp = mqtt.isWifiConnected();
  const bool mqttUp = mqtt.isMqttConnected();

  // WiFi serial server is started in onConnectionEstablished() when enabled

  // Log transitions to connected state (one-time per connect)
  if (wifiUp && !g_prevWifiUp)
  {
    Serial.printf("[Wi-Fi] Connected to '%s' (IP: %s, RSSI: %d dBm)\n",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }
  if (mqttUp && !g_prevMqttUp)
  {
    Serial.printf("[MQTT] Connected to %s:%d as '%s'\n",
                  mqtt.getMqttServerIp(), (int)mqtt.getMqttServerPort(), mqtt.getMqttClientName());
  }
  g_prevWifiUp = wifiUp;
  g_prevMqttUp = mqttUp;

  // Blink LED while offline (Wi-Fi or MQTT down)
  if (!(wifiUp && mqttUp))
  {
    if (millis() - g_lastLedBlinkMs >= OFFLINE_LED_BLINK_MS)
    {
      g_ledState = !g_ledState;
      digitalWrite(LED_BUILTIN, g_ledState ? LOW : HIGH); // active-low on many boards
      g_lastLedBlinkMs = millis();
    }
  }

  // Wi-Fi timeout and retry hinting (EspMQTTClient already retries; we add logs/force retry after timeout)
  if (!wifiUp)
  {
    if (g_wifiOfflineSince == 0)
      g_wifiOfflineSince = millis();
    // Start (or continue) Wi-Fi attempt timer
    if (g_wifiAttemptStartMs == 0)
      g_wifiAttemptStartMs = millis();

    // Periodic status log every ~5s so it doesn't look hung
    if (g_lastConnLogMs == 0 || millis() - g_lastConnLogMs > 5000)
    {
      wl_status_t st = WiFi.status();
      Serial.printf("[Wi-Fi] Connecting to '%s'... (status=%d: %s)\n", SECRET_WIFI_SSID, (int)st, wifiStatusToString(st));
      g_lastConnLogMs = millis();
    }

    // If a single attempt seems to stall for too long, force a fresh begin
    if (millis() - g_wifiAttemptStartMs > WIFI_CONNECT_TIMEOUT_MS)
    {
      Serial.println("[Wi-Fi] Connection attempt timed out. Forcing reconnect...");
      // Try a clean reconnect without blocking
      WiFi.disconnect(true);
      delay(50);
      WiFi.mode(WIFI_STA);
      WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASSWORD);
      g_wifiAttemptStartMs = millis();
    }

    // Safety reboot if offline too long (optional)
    if (OFFLINE_REBOOT_AFTER_MS > 0 && (millis() - g_wifiOfflineSince) > OFFLINE_REBOOT_AFTER_MS)
    {
      Serial.println("[Wi-Fi] Offline too long. Rebooting device to recover...");
      delay(200);
      ESP.restart();
    }
    // No further checks if Wi-Fi is down
    return;
  }
  else
  {
    // Wi-Fi is up
    g_wifiAttemptStartMs = 0;
    g_wifiOfflineSince = 0;
  }

  // MQTT timeout and status logging
  if (!mqttUp)
  {
    if (g_mqttOfflineSince == 0)
      g_mqttOfflineSince = millis();
    if (g_mqttAttemptStartMs == 0)
      g_mqttAttemptStartMs = millis();

    if (g_lastConnLogMs == 0 || millis() - g_lastConnLogMs > 5000)
    {
      Serial.printf("[MQTT] Connecting to %s:%d as '%s'...\n", mqtt.getMqttServerIp(), (int)mqtt.getMqttServerPort(), mqtt.getMqttClientName());
      g_lastConnLogMs = millis();
    }

    if (millis() - g_mqttAttemptStartMs > MQTT_CONNECT_TIMEOUT_MS)
    {
      Serial.println("[MQTT] Connection attempt seems slow. Will keep retrying in background.");
      // We don't force reconnect here because EspMQTTClient handles the schedule; just reset our timer for logging cadence
      g_mqttAttemptStartMs = millis();
    }

    if (OFFLINE_REBOOT_AFTER_MS > 0 && (millis() - g_mqttOfflineSince) > OFFLINE_REBOOT_AFTER_MS)
    {
      Serial.println("[MQTT] Offline too long. Rebooting device to recover...");
      delay(200);
      ESP.restart();
    }
    return;
  }
  else
  {
    // Both Wi-Fi and MQTT are up — ensure LED is steady off and reset timers
    if (!g_ledState)
    {
      digitalWrite(LED_BUILTIN, HIGH); // off
    }
    g_ledState = false;
    g_mqttAttemptStartMs = 0;
    g_mqttOfflineSince = 0;
    g_lastConnLogMs = 0;
  }
}
