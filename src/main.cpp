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

#include "private.h"        // Include private configuration (Wi-Fi, MQTT, etc.)
#include "everblu_meters.h" // Include EverBlu meter communication library
#include "version.h"        // Firmware version definition
#if defined(ESP8266)
#include <ESP8266WiFi.h> // Wi-Fi library for ESP8266
#include <ESP8266mDNS.h> // mDNS library for ESP8266
#include <EEPROM.h>      // EEPROM library for ESP8266
#elif defined(ESP32)
#include <WiFi.h>         // Wi-Fi library for ESP32
#include <ESPmDNS.h>      // mDNS library for ESP32
#include <Preferences.h>  // Preferences library for ESP32
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

// Resolved reading time (UTC) which may be updated dynamically after a successful read
// Resolved reading time:
// - UTC fields: scheduled time in UTC
// - Local fields: scheduled time in UTC+offset (TIMEZONE_OFFSET_MINUTES)
// These may be updated dynamically after a successful read (auto-align).
static int g_readHourUtc = DEFAULT_READING_HOUR_UTC;
static int g_readMinuteUtc = DEFAULT_READING_MINUTE_UTC;
static int g_readHourLocal = DEFAULT_READING_HOUR_UTC;
static int g_readMinuteLocal = DEFAULT_READING_MINUTE_UTC;

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
const char *lastErrorMessage = "None";

// Frequency offset storage
#define EEPROM_SIZE 64
#define FREQ_OFFSET_ADDR 0
#define FREQ_OFFSET_MAGIC 0xABCD // Magic number to verify valid data
float storedFrequencyOffset = 0.0;
bool autoScanEnabled = (AUTO_SCAN_ENABLED != 0); // Enable automatic scan on first boot if no offset found
int successfulReadsBeforeAdapt = 0;              // Track successful reads for adaptive tuning
float cumulativeFreqError = 0.0;                 // Accumulate FREQEST readings for adaptive adjustment
const int ADAPT_THRESHOLD = 10;                  // Adapt frequency after N successful reads

#if defined(ESP32)
Preferences preferences;
#endif

// ============================================================================
// Frequency Management API
// ============================================================================

/**
 * @brief Save frequency offset to persistent storage
 *
 * Stores the frequency offset value to EEPROM (ESP8266) or Preferences (ESP32)
 * with a magic number for validation. This offset is applied on next boot.
 *
 * @param offset Frequency offset in MHz to be added to base frequency
 */
void saveFrequencyOffset(float offset);

/**
 * @brief Load frequency offset from persistent storage
 *
 * Retrieves previously saved frequency offset from EEPROM (ESP8266) or
 * Preferences (ESP32). Validates data integrity using magic number.
 *
 * @return Frequency offset in MHz, or 0.0 if no valid data found
 */
float loadFrequencyOffset();

/**
 * @brief Perform narrow-range frequency scan
 *
 * Scans a narrow frequency range (±0.003 MHz around current frequency)
 * with fine step resolution (0.0005 MHz) to find optimal meter frequency.
 * Used for fine-tuning when close to correct frequency.
 *
 * Updates global frequency offset if better frequency is found.
 */
void performFrequencyScan();

/**
 * @brief Perform wide-range initial frequency scan
 *
 * Performs comprehensive frequency scan over wider range (±0.030 MHz)
 * with coarser step resolution (0.001 MHz). Used on first boot or when
 * no stored offset exists. More time-consuming but covers larger uncertainty.
 *
 * Saves discovered offset to persistent storage on success.
 */
void performWideInitialScan();

// ============================================================================
// Frequency Management Implementation
// ============================================================================

/**
 * @brief Adaptive frequency tracking using FREQEST
 *
 * Accumulates frequency offset estimates from successful reads and gradually
 * adjusts radio frequency to track meter drift. Uses statistical averaging
 * (ADAPT_THRESHOLD reads) to avoid over-correction on noise.
 *
 * FREQEST register provides frequency error estimate in 2's complement format,
 * with resolution ~1.59 kHz per LSB (26 MHz crystal).
 *
 * Adjustment logic:
 * - Accumulate FREQEST over multiple reads
 * - After threshold reads, calculate average error
 * - If average > 2 kHz, apply 50% correction to avoid oscillation
 * - Save new offset to persistent storage
 * - Reinitialize CC1101 with corrected frequency
 *
 * @param freqest Frequency offset estimate from CC1101 FREQEST register (-128 to +127)
 */
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

int _retry = 0;
const int MAX_RETRIES = 5;                    // Maximum number of retry attempts
unsigned long lastFailedAttempt = 0;          // Timestamp of last failed attempt
const unsigned long RETRY_COOLDOWN = 3600000; // 1 hour cooldown in milliseconds

// Global variable to store the reading schedule (default from private.h)
const char *readingSchedule = DEFAULT_READING_SCHEDULE;

// Helper variables for generating serial-prefixed MQTT topics and entity IDs
// These must be initialized before EspMQTTClient is constructed (for LWT topic)
// Format: everblu/cyble/{METER_SERIAL}
// NOTE: METER_SERIAL must be defined as a numeric value in private.h
// Compile-time validation ensures METER_SERIAL is non-zero
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// Compile-time validation: Ensure METER_SERIAL is defined and non-zero
// This static_assert will fail at compile time if METER_SERIAL is 0 or undefined
#if !defined(METER_SERIAL)
#error "METER_SERIAL must be defined in private.h"
#endif
#if METER_SERIAL == 0
#error "METER_SERIAL cannot be zero - please set a valid meter serial number in private.h"
#endif

// Buffer size calculations for MQTT topics:
// - meterSerialStr: METER_SERIAL as string (max 10 digits for ULONG_MAX) + null = 16 bytes (safe)
// - mqttBaseTopic: "everblu/cyble/" (15) + METER_SERIAL (10) + null (1) = 26 bytes minimum
//   Buffer size: 64 bytes provides 2.4x safety margin for future extensions
// - mqttLwtTopic: mqttBaseTopic (25) + "/status" (7) + null (1) = 33 bytes minimum
//   Buffer size: 80 bytes provides 2.4x safety margin
char meterSerialStr[16] = TOSTRING(METER_SERIAL);                          // String representation of METER_SERIAL
char mqttBaseTopic[64] = "everblu/cyble/" TOSTRING(METER_SERIAL);          // Base MQTT topic
char mqttLwtTopic[80] = "everblu/cyble/" TOSTRING(METER_SERIAL) "/status"; // LWT status topic with serial number

// Standard buffer size for constructing MQTT topic strings
// Calculation: mqttBaseTopic (max 25) + longest suffix "/wifi_signal_percentage" (24) + null (1) = 50 bytes
// Buffer size: 80 bytes provides 1.6x safety margin for topic construction
#define MQTT_TOPIC_BUFFER_SIZE 80

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
  return (strcmp(s, "Monday-Friday") == 0 ||
          strcmp(s, "Monday-Saturday") == 0 ||
          strcmp(s, "Monday-Sunday") == 0);
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
    Serial.printf("WARNING: Invalid reading schedule '%s'. Falling back to 'Monday-Friday'.\n", readingSchedule);
    readingSchedule = "Monday-Friday";
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
  if (strcmp(readingSchedule, "Monday-Friday") == 0)
  {
    return ptm->tm_wday >= 1 && ptm->tm_wday <= 5; // Monday to Friday
  }
  else if (strcmp(readingSchedule, "Monday-Saturday") == 0)
  {
    return ptm->tm_wday >= 1 && ptm->tm_wday <= 6; // Monday to Saturday
  }
  else if (strcmp(readingSchedule, "Monday-Sunday") == 0)
  {
    return true; // Every day
  }
  return false;
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
  mqtt.publish(String(mqttBaseTopic) + "/active_reading", "true", true);
  mqtt.publish(String(mqttBaseTopic) + "/cc1101_state", "Reading", true);

  struct tmeter_data meter_data = get_meter_data(); // Fetch meter data

  // Get current UTC time
  time_t tnow = time(nullptr);
  struct tm *ptm = gmtime(&tnow);
  Serial.printf("Current date (UTC) : %04d/%02d/%02d %02d:%02d/%02d - %ld\n", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec, (long)tnow);

  char iso8601[128];
  strftime(iso8601, sizeof iso8601, "%FT%TZ", gmtime(&tnow));

  // Handle data retrieval failure (including first-layer protection rejecting
  // corrupted frames and returning zeros).
  if (meter_data.reads_counter == 0 || meter_data.liters == 0)
  {
    Serial.printf("Unable to retrieve data from meter (attempt %d/%d)\n", _retry + 1, MAX_RETRIES);

    if (_retry < MAX_RETRIES - 1)
    {
      // Schedule retry using callback instead of recursion to prevent stack overflow
      _retry++;
      static char errorMsg[64];
      snprintf(errorMsg, sizeof(errorMsg), "Retry %d/%d - No data received", _retry, MAX_RETRIES);
      lastErrorMessage = errorMsg;
      Serial.printf("Scheduling retry in 10 seconds... (next attempt %d/%d)\n", _retry + 1, MAX_RETRIES);
      mqtt.publish(String(mqttBaseTopic) + "/active_reading", "false", true);
      mqtt.publish(String(mqttBaseTopic) + "/cc1101_state", "Idle", true);
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
      Serial.printf("Max retries (%d) reached. Entering 1-hour cooldown period.\n", MAX_RETRIES);
      mqtt.publish(String(mqttBaseTopic) + "/active_reading", "false", true);
      mqtt.publish(String(mqttBaseTopic) + "/cc1101_state", "Idle", true);
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

  Serial.printf("Data from meter:\n");
  Serial.printf("Liters : %d\nBattery (in months) : %d\nCounter : %d\nRSSI : %d\nTime start : %s\nTime end : %s\n\n", meter_data.liters, meter_data.battery_left, meter_data.reads_counter, meter_data.rssi, timeStartFormatted, timeEndFormatted);
  Serial.printf("RSSI (dBm) : %d\n", meter_data.rssi_dbm);
  Serial.printf("RSSI (percentage) : %d\n", calculateMeterdBmToPercentage(meter_data.rssi_dbm));
  Serial.printf("Signal quality (LQI) : %d\n", meter_data.lqi);
  Serial.printf("Signal quality (LQI percentage) : %d\n", calculateLQIToPercentage(meter_data.lqi));

  // Publish meter data to MQTT (using char buffers instead of String)
  char valueBuffer[32];

  snprintf(valueBuffer, sizeof(valueBuffer), "%d", meter_data.liters);
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
    // Count valid historical values (non-zero)
    for (int i = 0; i < 13; i++)
    {
      if (meter_data.history[i] == 0)
        break;
      num_history++;
    }

    // If the history block was marked available but contained no
    // non‑zero entries, treat it as unavailable for this frame.
    if (num_history == 0)
    {
      Serial.println("WARN: history_available=true but no non-zero history entries found - skipping history publish for this frame");
      meter_data.history_available = false;
    }
  }

  if (meter_data.history_available)
  {

    Serial.printf("\n=== Historical Data (%d months) ===\n", num_history);
    Serial.println("Month  Volume (L)  Usage (L)");
    Serial.println("-----  ----------  ---------");

    // Calculate monthly consumption from the historical data
    // Format: {"history": [oldest_volume, ..., newest_volume], "monthly_usage": [month1_usage, ..., month13_usage]}
    // The attributes JSON can become relatively large when all 13 history
    // slots are populated. Use a sufficiently large buffer and carefully
    // track remaining space to avoid truncation that would yield malformed
    // JSON on MQTT.
    char historyJson[1024];
    int pos = 0;

    // Start JSON object
    int remaining = sizeof(historyJson) - pos;
    if (remaining <= 1)
    {
      Serial.println("ERROR: historyJson buffer too small before writing header - skipping history publish");
      // Nothing written, just bail out of history publishing for this frame.
      return;
    }
    pos += snprintf(historyJson + pos, remaining, "{\"history\":[");

    // Add historical volumes and print to serial
    for (int i = 0; i < num_history; i++)
    {
      remaining = sizeof(historyJson) - pos;
      if (remaining <= 1)
      {
        Serial.println("ERROR: historyJson buffer full while writing history array - truncating");
        break;
      }
      pos += snprintf(historyJson + pos, remaining, "%s%u",
                      (i > 0 ? "," : ""), meter_data.history[i]);

      // Calculate and display monthly usage
      uint32_t usage = 0;
      if (i > 0 && meter_data.history[i] > meter_data.history[i - 1])
      {
        usage = meter_data.history[i] - meter_data.history[i - 1];
      }
      Serial.printf(" -%02d   %10u  %9u\n", num_history - i, meter_data.history[i], usage);
    }

    // Calculate current month usage (difference from most recent historical reading).
    // Declare these outside of any goto targets to avoid crossing initialisation
    // when jumping to finalize_history_json.
    uint32_t currentMonthUsage = 0;
    uint32_t currentLiters = static_cast<uint32_t>(meter_data.liters);
    if (num_history > 0 && currentLiters > meter_data.history[num_history - 1])
    {
      currentMonthUsage = currentLiters - meter_data.history[num_history - 1];
    }
    Serial.printf("  Now  %10d  %9u (current month)\n", meter_data.liters, currentMonthUsage);
    Serial.println("===================================\n");

    // Add monthly usage calculations to JSON
    remaining = sizeof(historyJson) - pos;
    if (remaining <= 1)
    {
      Serial.println("ERROR: historyJson buffer full before monthly_usage - truncating");
      // Close what we have so far and publish best-effort JSON.
      historyJson[sizeof(historyJson) - 1] = '\0';
      Serial.printf("Publishing JSON attributes (%d bytes): %s\n\n", strlen(historyJson), historyJson);
      mqtt.publish(String(mqttBaseTopic) + "/liters_attributes", historyJson, true);
      delay(5);
      Serial.printf("> Published %d months historical data (current month usage: %u L)\n",
                    num_history, currentMonthUsage);
      goto skip_history_publish;
    }
    pos += snprintf(historyJson + pos, remaining, "],\"monthly_usage\":[");
    for (int i = 0; i < num_history; i++)
    {
      uint32_t usage;
      if (i == 0)
      {
        // For oldest month, we can't calculate usage without an older baseline
        usage = 0;
      }
      else if (meter_data.history[i] > meter_data.history[i - 1])
      {
        // Calculate consumption as difference between consecutive months
        usage = meter_data.history[i] - meter_data.history[i - 1];
      }
      else
      {
        usage = 0; // Sanity check - shouldn't go backwards
      }
      remaining = sizeof(historyJson) - pos;
      if (remaining <= 1)
      {
        Serial.println("ERROR: historyJson buffer full while writing monthly_usage - truncating");
        break;
      }
      pos += snprintf(historyJson + pos, remaining, "%s%u",
                      (i > 0 ? "," : ""), usage);
    }

    remaining = sizeof(historyJson) - pos;
    if (remaining <= 1)
    {
      Serial.println("ERROR: historyJson buffer full before tail - truncating");
      historyJson[sizeof(historyJson) - 1] = '\0';
      Serial.printf("Publishing JSON attributes (%d bytes): %s\n\n", strlen(historyJson), historyJson);
      mqtt.publish(String(mqttBaseTopic) + "/liters_attributes", historyJson, true);
      delay(5);
      Serial.printf("> Published %d months historical data (current month usage: %u L)\n",
                    num_history, currentMonthUsage);
      goto skip_history_publish;
    }
    pos += snprintf(historyJson + pos, remaining,
                    "],\"current_month_usage\":%u,\"months_available\":%d}",
                    currentMonthUsage, num_history);

    // Ensure null termination even if we had to truncate early.
    historyJson[sizeof(historyJson) - 1] = '\0';

    Serial.printf("Publishing JSON attributes (%d bytes): %s\n\n", strlen(historyJson), historyJson);
    mqtt.publish(String(mqttBaseTopic) + "/liters_attributes", historyJson, true);
    delay(5);

    Serial.printf("> Published %d months historical data (current month usage: %u L)\n",
                  num_history, currentMonthUsage);
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
  sprintf(json, jsonTemplate, meter_data.liters, meter_data.reads_counter, meter_data.battery_left, meter_data.rssi, iso8601);
  mqtt.publish(String(mqttBaseTopic) + "/json", json, true);
  delay(5);

#if AUTO_ALIGN_READING_TIME
  // Optionally auto-align the daily scheduled reading time to the meter's wake window
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
  mqtt.publish(String(mqttBaseTopic) + "/cc1101_state", "Idle", true);
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
  adaptiveFrequencyTracking(meter_data.freqest);

  Serial.printf("Data update complete.\n\n");
}

// Function: onScheduled
// Function: onScheduled
// Description: Schedules daily meter readings at the configured local-offset time.
void onScheduled()
{
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
  json += "    \"name\": \"Water Meter " + String(METER_SERIAL) + "\",\n";
  json += "    \"mdl\": \"Itron EverBlu Cyble Enhanced Water Meter ESP8266/ESP32\",\n";
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
  Serial.println("> Publish meter settings");

  // Publish Meter Year, Serial (using char buffers instead of String)
  char valueBuffer[16];
  char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];

  snprintf(valueBuffer, sizeof(valueBuffer), "%d", METER_YEAR);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/water_meter_year", mqttBaseTopic);
  mqtt.publish(topicBuffer, valueBuffer, true);
  delay(5);

  snprintf(valueBuffer, sizeof(valueBuffer), "%u", METER_SERIAL);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/water_meter_serial", mqttBaseTopic);
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
// @param entity The entity name suffix (e.g., "water_meter_value")
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

  // Reading (Total) - Main water sensor
  Serial.println("> Publishing Water Usage sensor discovery...");
  json = "{\n";
  json += "  \"name\": \"Reading (Total)\",\n";
  json += "  \"uniq_id\": \"" + String(METER_SERIAL) + "_water_meter_value\",\n";
  json += "  \"obj_id\": \"" + String(METER_SERIAL) + "_water_meter_value\",\n";
  json += "  \"ic\": \"mdi:water\",\n";
  json += "  \"unit_of_meas\": \"L\",\n";
  json += "  \"dev_cla\": \"water\",\n";
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
  publishDiscoveryMessage("sensor", "water_meter_value", json);

  // Read Counter
  json = "{\n";
  json += "  \"name\": \"Read Counter\",\n";
  json += "  \"uniq_id\": \"" + String(METER_SERIAL) + "_water_meter_counter\",\n";
  json += "  \"obj_id\": \"" + String(METER_SERIAL) + "_water_meter_counter\",\n";
  json += "  \"ic\": \"mdi:counter\",\n";
  json += "  \"qos\": 0,\n";
  json += "  \"avty_t\": \"" + String(mqttBaseTopic) + "/status\",\n";
  json += "  \"stat_t\": \"" + String(mqttBaseTopic) + "/counter\",\n";
  json += "  \"frc_upd\": true,\n";
  json += "  \"dev\": {\n    " + buildDeviceJson() + "\n  }\n";
  json += "}";
  publishDiscoveryMessage("sensor", "water_meter_counter", json);

  // Last Read (timestamp)
  json = "{\n";
  json += "  \"name\": \"Last Read\",\n";
  json += "  \"uniq_id\": \"" + String(METER_SERIAL) + "_water_meter_timestamp\",\n";
  json += "  \"obj_id\": \"" + String(METER_SERIAL) + "_water_meter_timestamp\",\n";
  json += "  \"ic\": \"mdi:clock\",\n";
  json += "  \"dev_cla\": \"timestamp\",\n";
  json += "  \"qos\": 0,\n";
  json += "  \"avty_t\": \"" + String(mqttBaseTopic) + "/status\",\n";
  json += "  \"stat_t\": \"" + String(mqttBaseTopic) + "/timestamp\",\n";
  json += "  \"frc_upd\": true,\n";
  json += "  \"dev\": {\n    " + buildDeviceJson() + "\n  }\n";
  json += "}";
  publishDiscoveryMessage("sensor", "water_meter_timestamp", json);

  // Request Reading Button
  json = "{\n";
  json += "  \"name\": \"Request Reading Now\",\n";
  json += "  \"uniq_id\": \"" + String(METER_SERIAL) + "_water_meter_request\",\n";
  json += "  \"obj_id\": \"" + String(METER_SERIAL) + "_water_meter_request\",\n";
  json += "  \"qos\": 0,\n";
  json += "  \"avty_t\": \"" + String(mqttBaseTopic) + "/status\",\n";
  json += "  \"cmd_t\": \"" + String(mqttBaseTopic) + "/trigger_force\",\n";
  json += "  \"pl_avail\": \"online\",\n";
  json += "  \"pl_not_avail\": \"offline\",\n";
  json += "  \"pl_prs\": \"update\",\n";
  json += "  \"frc_upd\": true,\n";
  json += "  \"dev\": {\n    " + buildDeviceJson() + "\n  }\n";
  json += "}";
  publishDiscoveryMessage("button", "water_meter_request", json);

  // Diagnostic sensors
  publishDiscoveryMessage("sensor", "water_meter_wifi_ip", buildDiscoveryJson("IP Address", "wifi_ip", "mdi:ip-network-outline", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "water_meter_wifi_rssi", buildDiscoveryJson("WiFi RSSI", "wifi_rssi", "mdi:signal-variant", "dBm", "signal_strength", "measurement", "diagnostic"));
  publishDiscoveryMessage("sensor", "water_meter_mac_address", buildDiscoveryJson("MAC Address", "mac_address", "mdi:network", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "water_meter_wifi_ssid", buildDiscoveryJson("WiFi SSID", "wifi_ssid", "mdi:help-network-outline", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "water_meter_wifi_bssid", buildDiscoveryJson("WiFi BSSID", "wifi_bssid", "mdi:access-point-network", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "water_meter_uptime", buildDiscoveryJson("Device Uptime", "uptime", nullptr, nullptr, "timestamp", nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "water_meter_wifi_signal_percentage", buildDiscoveryJson("WiFi Signal", "wifi_signal_percentage", "mdi:wifi", "%", nullptr, "measurement", "diagnostic"));
  publishDiscoveryMessage("sensor", "water_meter_reading_time", buildDiscoveryJson("Reading Time (UTC)", "reading_time", "mdi:clock-outline", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "water_meter_reading_schedule", buildDiscoveryJson("Reading Schedule", "reading_schedule", "mdi:calendar-clock", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "water_meter_year", buildDiscoveryJson("Meter Year", "water_meter_year", "mdi:calendar", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "water_meter_serial", buildDiscoveryJson("Meter Serial", "water_meter_serial", "mdi:barcode", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "water_meter_battery_months", buildDiscoveryJson("Months Remaining", "battery", "mdi:battery-clock", "months", nullptr, "measurement", nullptr));
  publishDiscoveryMessage("sensor", "water_meter_rssi_dbm", buildDiscoveryJson("RSSI", "rssi_dbm", "mdi:signal", "dBm", "signal_strength", "measurement", nullptr));
  publishDiscoveryMessage("sensor", "water_meter_rssi_percentage", buildDiscoveryJson("Signal", "rssi_percentage", "mdi:signal-cellular-3", "%", nullptr, "measurement", nullptr));
  publishDiscoveryMessage("sensor", "water_meter_lqi_percentage", buildDiscoveryJson("Signal Quality (LQI)", "lqi_percentage", "mdi:signal-cellular-outline", "%", nullptr, "measurement", nullptr));
  publishDiscoveryMessage("sensor", "water_meter_time_start", buildDiscoveryJson("Wake Time", "time_start", "mdi:clock-start", nullptr, nullptr, nullptr, nullptr));
  publishDiscoveryMessage("sensor", "water_meter_time_end", buildDiscoveryJson("Sleep Time", "time_end", "mdi:clock-end", nullptr, nullptr, nullptr, nullptr));
  publishDiscoveryMessage("sensor", "water_meter_total_attempts", buildDiscoveryJson("Total Read Attempts", "total_attempts", "mdi:counter", nullptr, nullptr, "total_increasing", "diagnostic"));
  publishDiscoveryMessage("sensor", "water_meter_successful_reads", buildDiscoveryJson("Successful Reads", "successful_reads", "mdi:check-circle", nullptr, nullptr, "total_increasing", "diagnostic"));
  publishDiscoveryMessage("sensor", "water_meter_failed_reads", buildDiscoveryJson("Failed Reads", "failed_reads", "mdi:alert-circle", nullptr, nullptr, "total_increasing", "diagnostic"));
  publishDiscoveryMessage("sensor", "water_meter_last_error", buildDiscoveryJson("Last Error", "last_error", "mdi:alert", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "water_meter_cc1101_state", buildDiscoveryJson("CC1101 State", "cc1101_state", "mdi:radio-tower", nullptr, nullptr, nullptr, "diagnostic"));
  publishDiscoveryMessage("sensor", "water_meter_freq_offset", buildDiscoveryJson("Frequency Offset", "frequency_offset", "mdi:sine-wave", "kHz", nullptr, nullptr, "diagnostic"));

  // Buttons
  json = "{\n";
  json += "  \"name\": \"Restart Device\",\n";
  json += "  \"uniq_id\": \"" + String(METER_SERIAL) + "_water_meter_restart\",\n";
  json += "  \"obj_id\": \"" + String(METER_SERIAL) + "_water_meter_restart\",\n";
  json += "  \"qos\": 0,\n";
  json += "  \"avty_t\": \"" + String(mqttBaseTopic) + "/status\",\n";
  json += "  \"cmd_t\": \"" + String(mqttBaseTopic) + "/restart\",\n";
  json += "  \"pl_prs\": \"restart\",\n";
  json += "  \"ent_cat\": \"config\",\n";
  json += "  \"dev\": {\n    " + buildDeviceJson() + "\n  }\n";
  json += "}";
  publishDiscoveryMessage("button", "water_meter_restart", json);

  json = "{\n";
  json += "  \"name\": \"Scan Frequency\",\n";
  json += "  \"uniq_id\": \"" + String(METER_SERIAL) + "_water_meter_freq_scan\",\n";
  json += "  \"obj_id\": \"" + String(METER_SERIAL) + "_water_meter_freq_scan\",\n";
  json += "  \"ic\": \"mdi:magnify-scan\",\n";
  json += "  \"qos\": 0,\n";
  json += "  \"avty_t\": \"" + String(mqttBaseTopic) + "/status\",\n";
  json += "  \"cmd_t\": \"" + String(mqttBaseTopic) + "/frequency_scan\",\n";
  json += "  \"pl_prs\": \"scan\",\n";
  json += "  \"ent_cat\": \"config\",\n";
  json += "  \"dev\": {\n    " + buildDeviceJson() + "\n  }\n";
  json += "}";
  publishDiscoveryMessage("button", "water_meter_freq_scan", json);

  // Binary sensor for active reading
  json = "{\n";
  json += "  \"name\": \"Active Reading\",\n";
  json += "  \"uniq_id\": \"" + String(METER_SERIAL) + "_water_meter_active_reading\",\n";
  json += "  \"obj_id\": \"" + String(METER_SERIAL) + "_water_meter_active_reading\",\n";
  json += "  \"dev_cla\": \"running\",\n";
  json += "  \"qos\": 0,\n";
  json += "  \"avty_t\": \"" + String(mqttBaseTopic) + "/status\",\n";
  json += "  \"stat_t\": \"" + String(mqttBaseTopic) + "/active_reading\",\n";
  json += "  \"pl_on\": \"true\",\n";
  json += "  \"pl_off\": \"false\",\n";
  json += "  \"dev\": {\n    " + buildDeviceJson() + "\n  }\n";
  json += "}";
  publishDiscoveryMessage("binary_sensor", "water_meter_active_reading", json);

  Serial.println("> Home Assistant discovery messages published");
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
      Serial.printf("WARN: Invalid force-trigger command '%s' (expected 'update' or 'read')\n", message.c_str());
      char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
      snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
      mqtt.publish(topicBuffer, "Invalid trigger command", true);
      return;
    }

    Serial.printf("Force update requested via MQTT (command: %s) - overriding cooldown\n", message.c_str());

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
    performFrequencyScan(); });

  Serial.println("> Send MQTT config for HA.");

  // Publish all Home Assistant discovery messages with serial-specific entity IDs
  publishHADiscovery();

  // Set initial state for active reading
  char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/active_reading", mqttBaseTopic);
  mqtt.publish(topicBuffer, "false", true);
  delay(5);

  // Publish initial diagnostic metrics (using char buffers instead of String)
  char metricBuffer[16];

  snprintf(topicBuffer, sizeof(topicBuffer), "%s/cc1101_state", mqttBaseTopic);
  mqtt.publish(topicBuffer, "Idle", true);
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
  snprintf(freqBuffer, sizeof(freqBuffer), "%.3f", storedFrequencyOffset * 1000.0); // Convert MHz to kHz
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

// Function: saveFrequencyOffset
// Description: Saves the frequency offset to persistent storage (EEPROM for ESP8266, Preferences for ESP32)
void saveFrequencyOffset(float offset)
{
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
float loadFrequencyOffset()
{
#if defined(ESP8266)
  // ESP8266: Use EEPROM
  uint16_t magic = 0;
  EEPROM.get(FREQ_OFFSET_ADDR, magic);
  if (magic == FREQ_OFFSET_MAGIC)
  {
    float offset = 0.0;
    EEPROM.get(FREQ_OFFSET_ADDR + 2, offset);
    // Sanity check: offset should be reasonable (within ±0.1 MHz)
    if (offset >= -0.1 && offset <= 0.1)
    {
      Serial.printf("> Loaded frequency offset %.6f MHz from EEPROM\n", offset);
      return offset;
    }
    else
    {
      Serial.printf("> Invalid frequency offset %.6f MHz in EEPROM, using 0.0\n", offset);
    }
  }
  else
  {
    Serial.println("> No valid frequency offset found in EEPROM");
  }
#elif defined(ESP32)
  // ESP32: Use Preferences
  preferences.begin("everblu", true); // Read-only
  if (preferences.isKey("freq_offset"))
  {
    float offset = preferences.getFloat("freq_offset", 0.0);
    preferences.end();
    // Sanity check: offset should be reasonable (within ±0.1 MHz)
    if (offset >= -0.1 && offset <= 0.1)
    {
      Serial.printf("> Loaded frequency offset %.6f MHz from Preferences\n", offset);
      return offset;
    }
    else
    {
      Serial.printf("> Invalid frequency offset %.6f MHz in Preferences, using 0.0\n", offset);
    }
  }
  else
  {
    Serial.println("> No frequency offset found in Preferences");
    preferences.end();
  }
#endif
  return 0.0;
}

// Function: performFrequencyScan
// Description: Scans nearby frequencies to find the best signal and updates the offset
void performFrequencyScan()
{
  Serial.println("> Starting frequency scan...");
  Serial.println("> NOTE: Wi-Fi/MQTT connections may temporarily drop and reconnect while the scan is running. This is expected.");
  char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/cc1101_state", mqttBaseTopic);
  mqtt.publish(topicBuffer, "Frequency Scanning", true);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
  mqtt.publish(topicBuffer, "Performing frequency scan", true);

  float baseFreq = FREQUENCY;
  float bestFreq = baseFreq;
  int bestRSSI = -120; // Start with very low RSSI

  // Scan range: ±30 kHz in 5 kHz steps (±0.03 MHz in 0.005 MHz steps)
  float scanStart = baseFreq - 0.03;
  float scanEnd = baseFreq + 0.03;
  float scanStep = 0.005;

  Serial.printf("> Scanning from %.6f to %.6f MHz (step: %.6f MHz)\n", scanStart, scanEnd, scanStep);

  for (float freq = scanStart; freq <= scanEnd; freq += scanStep)
  {
    FEED_WDT(); // Feed watchdog for each frequency step
    // Reinitialize CC1101 with this frequency
    cc1101_init(freq);
    delay(50); // Allow time for frequency to settle

    // Try to get meter data (with short timeout)
    struct tmeter_data test_data = get_meter_data();

    if (test_data.rssi_dbm > bestRSSI && test_data.reads_counter > 0)
    {
      bestRSSI = test_data.rssi_dbm;
      bestFreq = freq;
      Serial.printf("> Better signal at %.6f MHz: RSSI=%d dBm\n", freq, test_data.rssi_dbm);
    }
  }

  // Calculate and save the offset
  float offset = bestFreq - baseFreq;
  Serial.printf("> Frequency scan complete. Best frequency: %.6f MHz (offset: %.6f MHz, RSSI: %d dBm)\n",
                bestFreq, offset, bestRSSI);

  if (bestRSSI > -120)
  { // Only save if we found something
    saveFrequencyOffset(offset);

    char freqBuffer[16];
    snprintf(freqBuffer, sizeof(freqBuffer), "%.3f", offset * 1000.0); // Convert MHz to kHz
    snprintf(topicBuffer, sizeof(topicBuffer), "%s/frequency_offset", mqttBaseTopic);
    mqtt.publish(topicBuffer, freqBuffer, true);

    char statusMsg[128];
    snprintf(statusMsg, sizeof(statusMsg), "Scan complete: offset %.3f kHz, RSSI %d dBm", offset * 1000.0, bestRSSI);
    snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
    mqtt.publish(topicBuffer, statusMsg, true);

    // Reinitialize with the best frequency
    cc1101_init(bestFreq);
  }
  else
  {
    Serial.println("> Frequency scan failed - no valid signal found");
    snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
    mqtt.publish(topicBuffer, "Frequency scan failed - no signal", true);
    // Restore original frequency
    cc1101_init(baseFreq + storedFrequencyOffset);
  }

  snprintf(topicBuffer, sizeof(topicBuffer), "%s/cc1101_state", mqttBaseTopic);
  mqtt.publish(topicBuffer, "Idle", true);
}

// Function: performWideInitialScan
// Description: Performs a wide-band scan on first boot to find meter frequency automatically
//              Scans ±100 kHz around the configured frequency in larger steps for faster discovery
void performWideInitialScan()
{
  Serial.println("> Performing wide initial scan (first boot - no saved offset)...");
  char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/cc1101_state", mqttBaseTopic);
  mqtt.publish(topicBuffer, "Initial Frequency Scan", true);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
  mqtt.publish(topicBuffer, "First boot: scanning for meter frequency", true);

  float baseFreq = FREQUENCY;
  float bestFreq = baseFreq;
  int bestRSSI = -120;

  // Wide scan: ±100 kHz in 10 kHz steps for faster initial discovery
  float scanStart = baseFreq - 0.10;
  float scanEnd = baseFreq + 0.10;
  float scanStep = 0.010;

  Serial.printf("> Wide scan from %.6f to %.6f MHz (step: %.6f MHz)\n", scanStart, scanEnd, scanStep);
  Serial.println("> This may take 1-2 minutes on first boot...");

  for (float freq = scanStart; freq <= scanEnd; freq += scanStep)
  {
    FEED_WDT(); // Feed watchdog for each frequency step
    cc1101_init(freq);
    delay(100); // Longer delay for frequency to settle during wide scan

    struct tmeter_data test_data = get_meter_data();

    if (test_data.rssi_dbm > bestRSSI && test_data.reads_counter > 0)
    {
      bestRSSI = test_data.rssi_dbm;
      bestFreq = freq;
      Serial.printf("> Found signal at %.6f MHz: RSSI=%d dBm\n", freq, test_data.rssi_dbm);
    }
  }

  if (bestRSSI > -120)
  {
    // Found a signal, now do a fine scan around it
    Serial.printf("> Performing fine scan around %.6f MHz...\n", bestFreq);
    float fineStart = bestFreq - 0.015;
    float fineEnd = bestFreq + 0.015;
    float fineStep = 0.003;
    int fineBestRSSI = bestRSSI;
    float fineBestFreq = bestFreq;

    for (float freq = fineStart; freq <= fineEnd; freq += fineStep)
    {
      FEED_WDT(); // Feed watchdog for each frequency step
      cc1101_init(freq);
      delay(50);

      struct tmeter_data test_data = get_meter_data();

      if (test_data.rssi_dbm > fineBestRSSI && test_data.reads_counter > 0)
      {
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
    snprintf(freqBuffer, sizeof(freqBuffer), "%.3f", offset * 1000.0); // Convert MHz to kHz
    snprintf(topicBuffer, sizeof(topicBuffer), "%s/frequency_offset", mqttBaseTopic);
    mqtt.publish(topicBuffer, freqBuffer, true);

    char statusMsg[128];
    snprintf(statusMsg, sizeof(statusMsg), "Initial scan complete: offset %.3f kHz", offset * 1000.0);
    snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
    mqtt.publish(topicBuffer, statusMsg, true);

    cc1101_init(bestFreq);
  }
  else
  {
    Serial.println("> Wide scan failed - no meter signal found!");
    Serial.println("> Please check:");
    Serial.println(">  1. Meter is within range (< 50m typically)");
    Serial.println(">  2. Antenna is connected to CC1101");
    Serial.println(">  3. Meter serial/year are correct in private.h");
    Serial.println(">  4. Current time is within meter's wake hours");
    snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
    mqtt.publish(topicBuffer, "Initial scan failed - check setup", true);
    cc1101_init(baseFreq);
  }

  snprintf(topicBuffer, sizeof(topicBuffer), "%s/cc1101_state", mqttBaseTopic);
  mqtt.publish(topicBuffer, "Idle", true);
}

// Function: adaptiveFrequencyTracking
// Description: Uses FREQEST register to adaptively adjust frequency offset over time
//              Accumulates frequency error estimates and adjusts when threshold is reached
void adaptiveFrequencyTracking(int8_t freqest)
{
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
  if (successfulReadsBeforeAdapt >= ADAPT_THRESHOLD)
  {
    float avgError = cumulativeFreqError / ADAPT_THRESHOLD;

    // Only adjust if average error is significant (> 2 kHz)
    if (abs(avgError * 1000) > 2.0)
    {
      Serial.printf("> Adaptive adjustment: average error %.4f kHz over %d reads\n",
                    avgError * 1000, ADAPT_THRESHOLD);

      // Adjust the stored offset (apply 50% of the measured error to avoid over-correction)
      float adjustment = avgError * 0.5;
      storedFrequencyOffset += adjustment;

      Serial.printf("> Adjusting frequency offset by %.6f MHz (new offset: %.6f MHz)\n",
                    adjustment, storedFrequencyOffset);

      saveFrequencyOffset(storedFrequencyOffset);

      char freqBuffer[16];
      char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
      snprintf(freqBuffer, sizeof(freqBuffer), "%.3f", storedFrequencyOffset * 1000.0); // Convert MHz to kHz
      snprintf(topicBuffer, sizeof(topicBuffer), "%s/frequency_offset", mqttBaseTopic);
      mqtt.publish(topicBuffer, freqBuffer, true);

      // Reinitialize CC1101 with adjusted frequency
      cc1101_init(FREQUENCY + storedFrequencyOffset);
    }
    else
    {
      Serial.printf("> Frequency stable (avg error %.4f kHz < 2 kHz threshold)\n", avgError * 1000);
    }

    // Reset accumulators
    cumulativeFreqError = 0.0;
    successfulReadsBeforeAdapt = 0;
  }
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
  if (METER_YEAR > 99)
  {
    Serial.printf("ERROR: Invalid METER_YEAR=%d (expected 0-99)\n", METER_YEAR);
    valid = false;
  }
  else
  {
    Serial.printf("✓ METER_YEAR: %d (20%02d)\n", METER_YEAR, METER_YEAR);
  }

  // Validate METER_SERIAL (should not be 0)
  if (METER_SERIAL == 0)
  {
    Serial.println("ERROR: METER_SERIAL not configured (value is 0)");
    Serial.println("       Update METER_SERIAL in private.h with your meter's serial number");
    valid = false;
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
  Serial.println("Water usage data for Home Assistant");
  Serial.println("https://github.com/genestealer/everblu-meters-esp8266-improved");
  Serial.printf("Target meter: 20%02d-%07lu\n\n", METER_YEAR, (unsigned long)METER_SERIAL);

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

  Serial.println("✓ Configuration valid - proceeding with initialization\n");

  // Note: mqttBaseTopic and meterSerialStr are initialized at global scope
  // to ensure they're ready when EspMQTTClient constructor runs
  Serial.printf("> MQTT base topic: %s\n", mqttBaseTopic);
  Serial.printf("> Meter serial string: %s\n", meterSerialStr);
  Serial.printf("> mqttBaseTopic length: %d\n", strlen(mqttBaseTopic));

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
  for (int i = 0; i < EEPROM_SIZE; i++)
  {
    EEPROM.write(i, 0xFF);
  }
  EEPROM.commit();
  Serial.println("> EEPROM cleared. Remember to set CLEAR_EEPROM_ON_BOOT = 0 after testing!");
#endif
#endif

  // Load stored frequency offset
  storedFrequencyOffset = loadFrequencyOffset();

  const bool noStoredOffset = (storedFrequencyOffset == 0.0f);

  // If no valid frequency offset found and auto-scan is enabled, perform wide initial scan
  if (noStoredOffset && autoScanEnabled)
  {
    Serial.println("> No stored frequency offset found. Performing wide initial scan...");
    performWideInitialScan();
    // Reload the frequency offset after scan
    storedFrequencyOffset = loadFrequencyOffset();
  }
  else if (noStoredOffset)
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
  Serial.println("WARNING: FREQUENCY not set in private.h; using default 433.820000 MHz (RADIAN).");
#endif

  // Optional functionalities of EspMQTTClient
#if ENABLE_MQTT_DEBUGGING
  mqtt.enableDebuggingMessages(true); // Enable debugging messages sent to serial output
  Serial.println(">> MQTT debugging enabled");
#endif

  // Set CC1101 radio frequency with automatic calibration
  Serial.println("> Initializing CC1101 radio...");
  float effectiveFrequency = FREQUENCY + storedFrequencyOffset;
  if (storedFrequencyOffset != 0.0)
  {
    Serial.printf("> Applying stored frequency offset: %.6f MHz (effective: %.6f MHz)\n",
                  storedFrequencyOffset, effectiveFrequency);
  }
  if (!cc1101_init(effectiveFrequency))
  {
    Serial.println("FATAL ERROR: CC1101 radio initialization failed!");
    Serial.println("Please check your wiring and connections.");
    while (true)
    {
      digitalWrite(LED_BUILTIN, LOW); // Blink LED to indicate error
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
      Serial.printf("[Wi-Fi] Connecting to '%s'... (status=%d: %s)\n", secret_wifi_ssid, (int)st, wifiStatusToString(st));
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
      WiFi.begin(secret_wifi_ssid, secret_wifi_password);
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
