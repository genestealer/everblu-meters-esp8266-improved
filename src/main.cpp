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

// Only include private.h in standalone builds, not ESPHome
#if !defined(USE_ESPHOME)
#include "private.h" // Include private configuration (Wi-Fi, MQTT, etc.)
#endif
#include "core/version.h"              // Firmware version definition
#include "core/logging.h"              // Timestamped serial logging macros
#include "core/wifi_serial.h"          // WiFi serial monitor
#include "core/cc1101.h"               // CC1101 RF transceiver and meter data
#include "core/meter_code_parser.h"    // Shared METER_CODE parser
#include "core/utils.h"                 // Utility functions
#include "services/schedule_manager.h"  // Schedule management
#include "services/frequency_manager.h" // Shared frequency calibration (scan/adaptive/storage)
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
// Options: presets ("Monday-Friday", "Monday-Saturday", "Monday-Sunday")
// or a specific day ("Monday" through "Sunday")
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

// Control whether the firmware performs the Deep auto scan on first boot
// 1 = enabled (default), 0 = disabled
#ifndef AUTO_SCAN_ENABLED
#define AUTO_SCAN_ENABLED 1
#endif

// Control whether the firmware automatically runs a frequency scan after a full
// streak of failed reads (when entering the cooldown period). This helps users
// who never trigger a manual scan recover from meter carrier-frequency drift.
// 0 = disabled (default), 1 = enabled
#ifndef AUTO_SCAN_ON_FAILURE_ENABLED
#define AUTO_SCAN_ON_FAILURE_ENABLED 0
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

// Flag to indicate if current data read is from scheduled trigger (vs manual MQTT command)
// Used to control whether auto-alignment should be applied to future scheduled reads
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

// Read success/failure metrics
unsigned long totalReadAttempts = 0;
unsigned long successfulReads = 0;
unsigned long failedReads = 0;
const char *lastErrorMessage = "None";

// CC1101 radio connection state
bool cc1101RadioConnected = false; // Tracks whether the radio is detected and initialized

// Frequency offset storage. The EEPROM/Preferences layout and all scan/adaptive
// state now live in the shared FrequencyManager (src/services/frequency_manager.cpp),
// which this build initializes in setup(). EEPROM_SIZE is retained only for the
// optional CLEAR_EEPROM_ON_BOOT maintenance path below.
#define EEPROM_SIZE 64
bool autoScanEnabled = (AUTO_SCAN_ENABLED != 0);                     // Enable automatic scan on first boot if no offset found
bool autoScanOnFailureEnabled = (AUTO_SCAN_ON_FAILURE_ENABLED != 0); // Enable automatic scan after max retries reached

// Define the adaptive frequency tracking threshold if missing from private.h
// Controls how many successful reads trigger a frequency adjustment
// Default: 1 (adjust after every read, ideal for once-per-day schedules)
// Increase to 5-10 for more stable reading conditions
#ifndef ADAPTIVE_THRESHOLD
#define ADAPTIVE_THRESHOLD 1
#endif
const int ADAPT_THRESHOLD = ADAPTIVE_THRESHOLD;

// ============================================================================
// Frequency Management API (thin MQTT wrappers over the shared FrequencyManager)
// ============================================================================

/**
 * @brief Perform a Deep frequency scan
 *
 * Scans +-150 kHz (default) around the configured frequency in fine 2.5 kHz steps,
 * mapping the response window then zooming to the exact carrier centre.
 * Also used on first boot when no stored offset exists.
 *
 * Saves discovered offset to persistent storage on success.
 */
void performDeepFrequencyScan(float scanRangeMHz = 0.150f, float scanStepMHz = 0.0025f);

/**
 * @brief Reset the persisted frequency offset to zero and re-tune the radio.
 */
void resetFrequencyOffset();

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
// Note: MQTT Client ID is made unique per device by appending the meter serial number
// This prevents multiple devices from having the same client ID and causing MQTT connection conflicts

static const char *buildMqttClientIdWithSerial()
{
  static char client_id[64] = {};
  uint8_t year = 0;
  uint32_t serial = 0;
  if (everblu::core::parseMeterCode(METER_CODE, &year, &serial))
  {
    snprintf(client_id, sizeof(client_id), "%s-%lu", SECRET_MQTT_CLIENT_ID, (unsigned long)serial);
  }
  else
  {
    // Keep startup resilient so validation can print a clear error afterward.
    snprintf(client_id, sizeof(client_id), "%s-%s", SECRET_MQTT_CLIENT_ID, METER_CODE);
  }
  return client_id;
}

const char *MQTT_CLIENT_ID_WITH_SERIAL = buildMqttClientIdWithSerial();

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
// mqttBaseTopic is populated during setup() after parsing METER_CODE.

const char jsonTemplate[] = "{ "
                            "\"liters\": %d, "
                            "\"counter\" : %d, "
                            "\"battery\" : %d, "
                            "\"rssi\" : %d, "
                            "\"timestamp\" : \"%s\" }";

// Define the default maximum retries if missing from the private.h file
#ifndef MAX_RETRIES
#define MAX_RETRIES 5 // Default: 5 retry attempts before cooldown
#endif

int _retry = 0;
const int max_retries = MAX_RETRIES;          // Maximum number of retry attempts (configurable in private.h)
unsigned long lastFailedAttempt = 0;          // Timestamp of last failed attempt
const unsigned long RETRY_COOLDOWN = 3600000; // 1 hour cooldown in milliseconds
bool g_autoScanAfterFailureDone = false;      // Guards the failure-recovery frequency scan to once per failure streak

// Global variable to store the reading schedule (default from private.h)
const char *readingSchedule = DEFAULT_READING_SCHEDULE;

// Helper variables for generating serial-prefixed MQTT topics and entity IDs.
// They are filled by parseMeterCode() in setup() before last-will configuration.
// Format: everblu/cyble/{serial} (or everblu/cyble if meter prefix is disabled)
// Compile-time check: Ensure METER_CODE is defined
#if !defined(METER_CODE)
#error "METER_CODE must be defined in private.h"
#endif

// Define meter prefix option if not defined in private.h (default to enabled)
#ifndef ENABLE_METER_PREFIX_IN_ENTITY_IDS
#define ENABLE_METER_PREFIX_IN_ENTITY_IDS 1
#endif

// Define Home Assistant MQTT discovery option if not defined in private.h (default to enabled)
#ifndef ENABLE_HA_DISCOVERY
#define ENABLE_HA_DISCOVERY 1
#endif

// Buffer sizes for MQTT topics are intentionally conservative for future-proofing.
// Current worst-case payloads are significantly smaller than these buffers.
// Parsed at runtime from METER_CODE by parseMeterCode() called at the start of setup()
static uint8_t g_meterYear = 0;
static uint32_t g_meterSerial = 0;
char meterSerialStr[16] = {}; // filled by parseMeterCode()
#if ENABLE_METER_PREFIX_IN_ENTITY_IDS
char mqttBaseTopic[64] = {}; // filled by parseMeterCode()
char mqttLwtTopic[80] = {};  // filled by parseMeterCode()
#else
char mqttBaseTopic[64] = "everblu/cyble";       // Base MQTT topic without meter serial
char mqttLwtTopic[80] = "everblu/cyble/status"; // LWT status topic without serial number
#endif

/**
 * Parse METER_CODE ("YY-SSSSSSS" or "YY-SSSSSSS-NNN") into g_meterYear,
 * g_meterSerial, and
 * the MQTT topic buffers.  Called once at the very start of setup().
 */
static void parseMeterCode()
{
  if (!everblu::core::parseMeterCode(METER_CODE, &g_meterYear, &g_meterSerial))
  {
    TS_PRINTLN("[ERROR] Invalid METER_CODE - expected YY-serial or YY-serial-NNN (with dashes)");
    return;
  }

  snprintf(meterSerialStr, sizeof(meterSerialStr), "%lu", (unsigned long)g_meterSerial);
#if ENABLE_METER_PREFIX_IN_ENTITY_IDS
  snprintf(mqttBaseTopic, sizeof(mqttBaseTopic), "everblu/cyble/%lu", (unsigned long)g_meterSerial);
  snprintf(mqttLwtTopic, sizeof(mqttLwtTopic), "everblu/cyble/%lu/status", (unsigned long)g_meterSerial);
#endif
}

// Standard buffer size for constructing MQTT topic strings
// Calculation: mqttBaseTopic (max 63) + longest suffix "/wifi_signal_percentage" (24) + null (1) = 88 bytes
// Buffer size: 128 bytes provides 1.45x safety margin for topic construction
#define MQTT_TOPIC_BUFFER_SIZE 128

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
    TS_PRINTLN("[STATUS] Meter type: GAS (readings in m³)");
  }
  else
  {
    meterDeviceClass = "water";
    meterIcon = "mdi:water";
    meterUnit = "L";
    TS_PRINTLN("[STATUS] Meter type: WATER (readings in L)");
  }
}

// ============================================================================
// Schedule Validation API
// ============================================================================

// Note: isValidReadingSchedule() is now in utils.h/cpp

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
    TS_PRINTF("[WARNING] Invalid reading schedule '%s'. Falling back to 'Monday-Friday'.\n", readingSchedule);
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

// Note: isReadingDay() is now in ScheduleManager class

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

// Note: calculateMeterdBmToPercentage() and calculateLQIToPercentage() are now in utils.h/cpp

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
  Serial.println("");
  Serial.println("========================================");
  Serial.printf("        METER READ - START (fw %s)\n", EVERBLU_FW_VERSION);
  Serial.println("========================================");
  TS_PRINTF("[STATUS] Updating data from meter...\n");
  TS_PRINTF("[STATUS] Retry count: %d\n", _retry);
  TS_PRINTF("[STATUS] Reading schedule: %s\n", readingSchedule);
  TS_PRINTF("[STATUS] Scheduled read time: %02d:%02d UTC (%02d:%02d local-offset)\n", g_readHourUtc, g_readMinuteUtc, g_readHourLocal, g_readMinuteLocal);

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
  Serial.println();
  TS_PRINTF("[TIME] Current date (UTC): %04d/%02d/%02d %02d:%02d:%02d - %ld\n", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec, (long)tnow);

  char iso8601[128];
  strftime(iso8601, sizeof iso8601, "%FT%TZ", gmtime(&tnow));

  // Handle data retrieval failure (including first-layer protection rejecting
  // corrupted frames and returning zeros).
  if (meter_data.reads_counter == 0 || meter_data.volume == 0)
  {
    TS_PRINTF("[ERROR] Unable to retrieve data from meter (attempt %d/%d)\n", _retry + 1, max_retries);

    if (_retry < max_retries - 1)
    {
      // Schedule retry using callback instead of recursion to prevent stack overflow
      _retry++;
      static char errorMsg[64];
      snprintf(errorMsg, sizeof(errorMsg), "Retry %d/%d - No data received", _retry, max_retries);
      lastErrorMessage = errorMsg;
      TS_PRINTF("[STATUS] Scheduling retry in 5 seconds... (next attempt %d/%d)\n", _retry + 1, max_retries);
      // Keep the "Active Reading" sensor true and the radio state as "Reading"
      // for the whole retry sequence so they don't flip to "Not running"/Idle
      // between attempts. They are cleared only on final success or after max
      // retries (see the else branch below).
      mqtt.publish(String(mqttBaseTopic) + "/last_error", lastErrorMessage, true);
      digitalWrite(LED_BUILTIN, HIGH); // Turn off LED
      // Use non-blocking callback instead of recursive call
      mqtt.executeDelayed(5000, onUpdateData);
    }
    else
    {
      // Max retries reached, enter cooldown period
      lastFailedAttempt = millis();
      failedReads++;
      lastErrorMessage = "Max retries reached - cooling down";
      TS_PRINTF("[ERROR] Max retries (%d) reached. Entering 1-hour cooldown period.\n", max_retries);
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

      // When the meter cannot be reached after all retries, a drifted carrier
      // frequency (crystal offset) is a common cause. Automatically run a
      // frequency scan once per failure streak so users who never trigger a
      // manual scan still get recalibrated. The guard is reset on the next
      // successful read so we don't burn power scanning on every cooldown when
      // the meter is genuinely unreachable (e.g. dead battery).
      if (autoScanOnFailureEnabled && !g_autoScanAfterFailureDone)
      {
        g_autoScanAfterFailureDone = true;
        TS_PRINTLN("[FREQ] Max retries reached - running narrow frequency scan (±20 kHz) to re-tune after drift... (disable: AUTO_SCAN_ON_FAILURE_ENABLED 0 in private.h)");
        performDeepFrequencyScan(0.020f, 0.001f); // ±20 kHz, 1 kHz steps, ~41 steps, ~2 min
      }
    }
    Serial.println("========================================");
    Serial.println("        METER READ - FAILED");
    Serial.println("========================================");
    Serial.println("");
    return;
  }

  // Format int time_start and time_end as "HH:MM"
  char timeStartFormatted[6];
  char timeEndFormatted[6];
  int timeStart = constrain(meter_data.time_start, 0, 23); // Ensure valid hour range
  int timeEnd = constrain(meter_data.time_end, 0, 23);     // Ensure valid hour range
  snprintf(timeStartFormatted, sizeof(timeStartFormatted), "%02d:00", timeStart);
  snprintf(timeEndFormatted, sizeof(timeEndFormatted), "%02d:00", timeEnd);

  // Use shared utility function to print meter data
  printMeterDataSummary(&meter_data, meterIsGas, GAS_VOLUME_DIVISOR);

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
      TS_PRINTLN("[WARN] history_available=true but no non-zero history entries found - skipping history publish for this frame");
      meter_data.history_available = false;
    }
  }

  if (meter_data.history_available)
  {

    Serial.printf("\n=== HISTORICAL DATA (%d months) ===\n", num_history);
    TS_PRINTLN("[HISTORY] Month  Volume (L)  Usage (L)");
    TS_PRINTLN("[HISTORY] -----  ----------  ---------");

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
      TS_PRINTLN("[ERROR] historyJson buffer too small before writing header - skipping history publish");
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
        TS_PRINTLN("[ERROR] historyJson buffer full while writing history array - truncating");
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
      TS_PRINTF("[HISTORY]  -%02d   %10u  %9u\n", num_history - i, meter_data.history[i], usage);
    }

    // Calculate current month usage (difference from most recent historical reading).
    // Declare these outside of any goto targets to avoid crossing initialisation
    // when jumping to finalize_history_json.
    uint32_t currentMonthUsage = 0;
    uint32_t currentVolume = static_cast<uint32_t>(meter_data.volume);
    if (num_history > 0 && currentVolume > meter_data.history[num_history - 1])
    {
      currentMonthUsage = currentVolume - meter_data.history[num_history - 1];
    }
    TS_PRINTF("[HISTORY]   Now  %10u  %9u (current month usage: %u L)\n", currentVolume, currentMonthUsage, currentMonthUsage);
    Serial.println("===================================\n");

    // Add monthly usage calculations to JSON
    remaining = sizeof(historyJson) - pos;
    if (remaining <= 1)
    {
      TS_PRINTLN("[ERROR] historyJson buffer full before monthly_usage - truncating");
      // Close what we have so far and publish best-effort JSON.
      historyJson[sizeof(historyJson) - 1] = '\0';
      TS_PRINTF("[MQTT] Publishing JSON attributes (%d bytes): %s\n\n", strlen(historyJson), historyJson);
      mqtt.publish(String(mqttBaseTopic) + "/liters_attributes", historyJson, true);
      delay(5);
      TS_PRINTF("[MQTT] Published %d months historical data (current month usage: %u L)\n",
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
        TS_PRINTLN("[ERROR] historyJson buffer full while writing monthly_usage - truncating");
        break;
      }
      pos += snprintf(historyJson + pos, remaining, "%s%u",
                      (i > 0 ? "," : ""), usage);
    }

    remaining = sizeof(historyJson) - pos;
    if (remaining <= 1)
    {
      TS_PRINTLN("[ERROR] historyJson buffer full before tail - truncating");
      historyJson[sizeof(historyJson) - 1] = '\0';
      TS_PRINTF("[MQTT] Publishing JSON attributes (%d bytes): %s\n\n", strlen(historyJson), historyJson);
      mqtt.publish(String(mqttBaseTopic) + "/liters_attributes", historyJson, true);
      delay(5);
      TS_PRINTF("[MQTT] Published %d months historical data (current month usage: %u L)\n",
                    num_history, currentMonthUsage);
      goto skip_history_publish;
    }
    pos += snprintf(historyJson + pos, remaining,
                    "],\"current_month_usage\":%u,\"months_available\":%d}",
                    currentMonthUsage, num_history);

    // Ensure null termination even if we had to truncate early.
    historyJson[sizeof(historyJson) - 1] = '\0';

    TS_PRINTF("[MQTT] Publishing JSON attributes (%d bytes): %s\n\n", strlen(historyJson), historyJson);
    mqtt.publish(String(mqttBaseTopic) + "/liters_attributes", historyJson, true);
    delay(5);

    TS_PRINTF("[MQTT] Published %d months historical data (current month usage: %u L)\n",
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

      TS_PRINTF("[SCHEDULE] Auto-aligned reading time to %02d:%02d local-offset (%02d:%02d UTC) (window %02d-%02d local)\n",
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
  g_autoScanAfterFailureDone = false; // Allow a fresh auto-scan on the next failure streak
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

  // Reset scheduled read flag for next invocation
  g_isScheduledRead = false;

  Serial.println("========================================");
  Serial.println("        METER READ - COMPLETE");
  Serial.println("========================================");
  Serial.println("");
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

  if (ScheduleManager::isReadingDay(ptm) && timeMatch && ptm->tm_sec == 0)
  {
    // Check if we're still in cooldown period after failed attempts
    if (lastFailedAttempt > 0 && (millis() - lastFailedAttempt) < RETRY_COOLDOWN)
    {
      unsigned long remainingCooldown = (RETRY_COOLDOWN - (millis() - lastFailedAttempt)) / 1000;
      TS_PRINTF("[WARN] Still in cooldown period. %lu seconds remaining.\n", remainingCooldown);

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

// Helper function to conditionally build meter prefix for entity IDs
// Returns meter serial with underscore suffix if ENABLE_METER_PREFIX_IN_ENTITY_IDS is 1, empty otherwise
String getMeterPrefix()
{
#if ENABLE_METER_PREFIX_IN_ENTITY_IDS
  return String(g_meterSerial) + "_";
#else
  return "";
#endif
}

// Helper function to build JSON device block with METER_SERIAL
String buildDeviceJson()
{
  // Pre-allocate memory to prevent heap fragmentation
  // Estimated size: ~250 bytes for device JSON
  String json;
  json.reserve(300);

#if ENABLE_METER_PREFIX_IN_ENTITY_IDS
  json = "\"ids\": [\"" + String(g_meterSerial) + "\"],\n";
  json += "    \"name\": \"EverBlu Meter " + String(g_meterSerial) + "\",\n";
#else
  // When meter prefix is disabled, use a fixed device ID for single-meter setup
  json = "\"ids\": [\"everblu_meter_device\"],\n";
  json += "    \"name\": \"EverBlu Meter\",\n";
#endif
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
  json += "  \"uniq_id\": \"" + getMeterPrefix() + String(entity_id) + "\",\n";
  json += "  \"obj_id\": \"" + getMeterPrefix() + String(entity_id) + "\",\n";
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
  TS_PRINTLN("[MQTT] Publish Wi-Fi details...");

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

  TS_PRINTLN("[MQTT] Wi-Fi details published");
}

// Function: publishMeterSettings
// Description: Publishes meter configuration (year, serial, frequency) to MQTT.
void publishMeterSettings()
{
  TS_PRINTLN("[MQTT] Publish meter settings...");

  // Publish Meter Year, Serial (using char buffers instead of String)
  char valueBuffer[16];
  char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];

  snprintf(valueBuffer, sizeof(valueBuffer), "%d", g_meterYear);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/everblu_meter_year", mqttBaseTopic);
  mqtt.publish(topicBuffer, valueBuffer, true);
  delay(5);

  snprintf(valueBuffer, sizeof(valueBuffer), "%lu", (unsigned long)g_meterSerial);
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

  TS_PRINTLN("[MQTT] Meter settings published");
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
  // optional METER_SERIAL (10) + "_" (1) + entity (50) + "/config" (7) + null (1) = ~97 bytes
  char configTopic[256];
  char entityId[128];
#if ENABLE_METER_PREFIX_IN_ENTITY_IDS
  snprintf(entityId, sizeof(entityId), "%lu_%s", (unsigned long)g_meterSerial, entity);
#else
  snprintf(entityId, sizeof(entityId), "%s", entity);
#endif
  snprintf(configTopic, sizeof(configTopic), "homeassistant/%s/%s/config", domain, entityId);
  mqtt.publish(configTopic, jsonPayload.c_str(), true);
  delay(5);
}

// Function: publishHADiscovery
// Description: Publishes all Home Assistant MQTT discovery messages with serial-specific entity IDs
void publishHADiscovery()
{
  TS_PRINTLN("[MQTT] Publishing Home Assistant discovery messages...");

  String json;

  // Reading (Total) - Main water/gas sensor
  TS_PRINTLN("[MQTT] Publishing Reading (Total) sensor discovery...");
  json = "{\n";
  json += "  \"name\": \"Reading (Total)\",\n";
  json += "  \"uniq_id\": \"" + getMeterPrefix() + "everblu_meter_value\",\n";
  json += "  \"obj_id\": \"" + getMeterPrefix() + "everblu_meter_value\",\n";
  json += "  \"ic\": \"" + String(meterIcon) + "\",\n";
  json += "  \"unit_of_meas\": \"" + String(meterUnit) + "\",\n";
  json += "  \"dev_cla\": \"" + String(meterDeviceClass) + "\",\n";
  json += "  \"stat_cla\": \"total_increasing\",\n";
  json += "  \"qos\": 0,\n";
  json += "  \"avty_t\": \"" + String(mqttBaseTopic) + "/status\",\n";
  json += "  \"stat_t\": \"" + String(mqttBaseTopic) + "/liters\",\n";
  TS_PRINTF("[MQTT] Water Usage state topic: %s/liters\n", mqttBaseTopic);
  json += "  \"json_attr_t\": \"" + String(mqttBaseTopic) + "/liters_attributes\",\n";
  json += "  \"sug_dsp_prc\": 0,\n";
  json += "  \"frc_upd\": true,\n";
  json += "  \"dev\": {\n    " + buildDeviceJson() + "\n  }\n";
  json += "}";
  publishDiscoveryMessage("sensor", "everblu_meter_value", json);

  // Read Counter
  json = "{\n";
  json += "  \"name\": \"Read Counter\",\n";
  json += "  \"uniq_id\": \"" + getMeterPrefix() + "everblu_meter_counter\",\n";
  json += "  \"obj_id\": \"" + getMeterPrefix() + "everblu_meter_counter\",\n";
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
  json += "  \"uniq_id\": \"" + getMeterPrefix() + "everblu_meter_timestamp\",\n";
  json += "  \"obj_id\": \"" + getMeterPrefix() + "everblu_meter_timestamp\",\n";
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
  json += "  \"uniq_id\": \"" + getMeterPrefix() + "everblu_meter_request\",\n";
  json += "  \"obj_id\": \"" + getMeterPrefix() + "everblu_meter_request\",\n";
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
  publishDiscoveryMessage("sensor", "everblu_meter_freq_offset", buildDiscoveryJson("Frequency Offset", "frequency_offset", "mdi:sine-wave", "kHz", nullptr, "measurement", "diagnostic"));
  publishDiscoveryMessage("sensor", "everblu_meter_tuned_frequency", buildDiscoveryJson("Tuned Frequency (MHz)", "tuned_frequency", "mdi:radio-tower", "MHz", nullptr, "measurement", "diagnostic"));
  publishDiscoveryMessage("sensor", "everblu_meter_freq_estimate", buildDiscoveryJson("Frequency Estimate", "frequency_estimate", "mdi:sine-wave", "kHz", nullptr, "measurement", "diagnostic"));

  // Buttons
  json = "{\n";
  json += "  \"name\": \"Restart Device\",\n";
  json += "  \"uniq_id\": \"" + getMeterPrefix() + "everblu_meter_restart\",\n";
  json += "  \"obj_id\": \"" + getMeterPrefix() + "everblu_meter_restart\",\n";
  json += "  \"qos\": 0,\n";
  json += "  \"avty_t\": \"" + String(mqttBaseTopic) + "/status\",\n";
  json += "  \"cmd_t\": \"" + String(mqttBaseTopic) + "/restart\",\n";
  json += "  \"pl_prs\": \"restart\",\n";
  json += "  \"ent_cat\": \"config\",\n";
  json += "  \"dev\": {\n    " + buildDeviceJson() + "\n  }\n";
  json += "}";
  publishDiscoveryMessage("button", "everblu_meter_restart", json);

  json = "{\n";
  json += "  \"name\": \"Deep Frequency Scan\",\n";
  json += "  \"uniq_id\": \"" + getMeterPrefix() + "everblu_meter_deep_scan\",\n";
  json += "  \"obj_id\": \"" + getMeterPrefix() + "everblu_meter_deep_scan\",\n";
  json += "  \"ic\": \"mdi:radar\",\n";
  json += "  \"qos\": 0,\n";
  json += "  \"avty_t\": \"" + String(mqttBaseTopic) + "/status\",\n";
  json += "  \"cmd_t\": \"" + String(mqttBaseTopic) + "/deep_scan\",\n";
  json += "  \"pl_prs\": \"scan\",\n";
  json += "  \"ent_cat\": \"config\",\n";
  json += "  \"dev\": {\n    " + buildDeviceJson() + "\n  }\n";
  json += "}";
  publishDiscoveryMessage("button", "everblu_meter_deep_scan", json);

  json = "{\n";
  json += "  \"name\": \"Reset Frequency Offset\",\n";
  json += "  \"uniq_id\": \"" + getMeterPrefix() + "everblu_meter_reset_frequency\",\n";
  json += "  \"obj_id\": \"" + getMeterPrefix() + "everblu_meter_reset_frequency\",\n";
  json += "  \"ic\": \"mdi:restore\",\n";
  json += "  \"qos\": 0,\n";
  json += "  \"avty_t\": \"" + String(mqttBaseTopic) + "/status\",\n";
  json += "  \"cmd_t\": \"" + String(mqttBaseTopic) + "/reset_frequency\",\n";
  json += "  \"pl_prs\": \"reset\",\n";
  json += "  \"ent_cat\": \"config\",\n";
  json += "  \"dev\": {\n    " + buildDeviceJson() + "\n  }\n";
  json += "}";
  publishDiscoveryMessage("button", "everblu_meter_reset_frequency", json);

  // Binary sensor for active reading
  json = "{\n";
  json += "  \"name\": \"Active Reading\",\n";
  json += "  \"uniq_id\": \"" + getMeterPrefix() + "everblu_meter_active_reading\",\n";
  json += "  \"obj_id\": \"" + getMeterPrefix() + "everblu_meter_active_reading\",\n";
  json += "  \"dev_cla\": \"running\",\n";
  json += "  \"qos\": 0,\n";
  json += "  \"avty_t\": \"" + String(mqttBaseTopic) + "/status\",\n";
  json += "  \"stat_t\": \"" + String(mqttBaseTopic) + "/active_reading\",\n";
  json += "  \"pl_on\": \"true\",\n";
  json += "  \"pl_off\": \"false\",\n";
  json += "  \"dev\": {\n    " + buildDeviceJson() + "\n  }\n";
  json += "}";
  publishDiscoveryMessage("binary_sensor", "everblu_meter_active_reading", json);

  TS_PRINTLN("[MQTT] Home Assistant discovery messages published");
}

// Function: onConnectionEstablished
// Description: Handles MQTT connection establishment, including Home Assistant discovery and OTA setup.
void onConnectionEstablished()
{
  TS_PRINTLN("[MQTT] Connected to MQTT Broker");

  TS_PRINTLN("[TIME] Configure time from NTP server. Please wait...");
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
    TS_PRINTF("[TIME] ✓ NTP sync successful after %lu ms\n", (unsigned long)(millis() - waitStart));
  }
  else
  {
    TS_PRINTF("[WARNING] NTP sync failed within %lu ms. Clock may be unset (epoch=%ld).\n",
                  (unsigned long)(millis() - waitStart), (long)tnow);
  }

  struct tm *ptm = gmtime(&tnow);
  TS_PRINTF("[TIME] current date (UTC) : %04d/%02d/%02d %02d:%02d:%02d - %ld\n", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec, (long)tnow);
  // Print simple offset and derived local time for debugging
  int offsetMin = TIMEZONE_OFFSET_MINUTES;
  time_t tlocal = tnow + (time_t)offsetMin * 60;
  struct tm *plocal = gmtime(&tlocal);
  TS_PRINTF("[TIME] Configured UTC offset: %+d minutes\n", offsetMin);
  TS_PRINTF("[TIME] Current date (UTC+offset): %04d/%02d/%02d %02d:%02d:%02d - %ld\n",
                plocal->tm_year + 1900, plocal->tm_mon + 1, plocal->tm_mday,
                plocal->tm_hour, plocal->tm_min, plocal->tm_sec, (long)tlocal);

  // Initialize schedule caches using validated UTC defaults
  updateResolvedScheduleFromUtc(DEFAULT_READING_HOUR_UTC, DEFAULT_READING_MINUTE_UTC);

  TS_PRINTLN("[OTA] Configure Arduino OTA flash.");
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
                        { TS_PRINTF("[OTA] %u%%\r\n", (progress / (total / 100))); });
  ArduinoOTA.onError([](ota_error_t error)
                     {
    if (error == OTA_AUTH_ERROR)    { TS_PRINTF("[OTA] Error[%u]: Auth Failed\n",    error); }
    else if (error == OTA_BEGIN_ERROR)   { TS_PRINTF("[OTA] Error[%u]: Begin Failed\n",   error); }
    else if (error == OTA_CONNECT_ERROR) { TS_PRINTF("[OTA] Error[%u]: Connect Failed\n", error); }
    else if (error == OTA_RECEIVE_ERROR) { TS_PRINTF("[OTA] Error[%u]: Receive Failed\n", error); }
    else if (error == OTA_END_ERROR)     { TS_PRINTF("[OTA] Error[%u]: End Failed\n",     error); }
    else                                 { TS_PRINTF("[OTA] Error[%u]: Unknown\n",         error); } });
  ArduinoOTA.setHostname("EVERBLUREADER");
  ArduinoOTA.begin();
  TS_PRINTF("[STATUS] IP address: %s\n", WiFi.localIP().toString().c_str());

  // Start WiFi serial monitor if enabled in configuration
#if WIFI_SERIAL_MONITOR_ENABLED
  wifiSerialBegin();
  TS_PRINTLN("[WIFI] WiFi Serial Monitor: ENABLED");
#else
  TS_PRINTLN("[WIFI] WiFi Serial Monitor: DISABLED");
#endif

  char triggerTopic[80];
  snprintf(triggerTopic, sizeof(triggerTopic), "%s/trigger", mqttBaseTopic);
  mqtt.subscribe(triggerTopic, [](const String &message)
                 {
    // Input validation: only accept whitelisted commands
    if (message != "update" && message != "read") {
      TS_PRINTF("[WARN] Invalid trigger command '%s' (expected 'update' or 'read')\n", message.c_str());
      char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
      snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
      mqtt.publish(topicBuffer, "Invalid trigger command", true);
      return;
    }

    // Check if we're in cooldown period
    if (lastFailedAttempt > 0 && (millis() - lastFailedAttempt) < RETRY_COOLDOWN) {
      unsigned long remainingCooldown = (RETRY_COOLDOWN - (millis() - lastFailedAttempt)) / 1000;
      TS_PRINTF("[WARN] Cannot trigger update: Still in cooldown period. %lu seconds remaining.\n", remainingCooldown);

      char cooldownMsg[64];
      snprintf(cooldownMsg, sizeof(cooldownMsg), "Cooldown active, %lus remaining", remainingCooldown);
      char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
      snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
      mqtt.publish(topicBuffer, cooldownMsg, true);
      return;
    }

    TS_PRINTF("[MQTT] Update data from meter from MQTT trigger (command: %s)\n", message.c_str());

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
      TS_PRINTF("[WARN] Invalid force-trigger command '%s' (expected 'update' or 'read')\n", message.c_str());
      char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
      snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
      mqtt.publish(topicBuffer, "Invalid trigger command", true);
      return;
    }

    TS_PRINTF("[STATUS] Force update requested via MQTT (command: %s) - overriding cooldown\n", message.c_str());

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
                     TS_PRINTF("[WARN] Invalid restart command '%s' (expected 'restart')\n", message.c_str());
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

  char wideFreqScanTopic[MQTT_TOPIC_BUFFER_SIZE];
  snprintf(wideFreqScanTopic, sizeof(wideFreqScanTopic), "%s/deep_scan", mqttBaseTopic);
  mqtt.subscribe(wideFreqScanTopic, [](const String &message)
                 {
    // Input validation: only accept "scan" command
    if (message != "scan") {
      TS_PRINTF("[WARN] Invalid deep scan command '%s' (expected 'scan')\n", message.c_str());
      char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
      snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
      mqtt.publish(topicBuffer, "Invalid scan command", true);
      return;
    }

    Serial.println("Deep frequency scan command received via MQTT");
    performDeepFrequencyScan(); });

  char resetFrequencyTopic[MQTT_TOPIC_BUFFER_SIZE];
  snprintf(resetFrequencyTopic, sizeof(resetFrequencyTopic), "%s/reset_frequency", mqttBaseTopic);
  mqtt.subscribe(resetFrequencyTopic, [](const String &message)
                 {
    // Input validation: only accept "reset" command
    if (message != "reset") {
      TS_PRINTF("[WARN] Invalid reset frequency command '%s' (expected 'reset')\n", message.c_str());
      char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
      snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
      mqtt.publish(topicBuffer, "Invalid reset command", true);
      return;
    }

    Serial.println("Reset frequency offset command received via MQTT");
    resetFrequencyOffset(); });

  // Publish Home Assistant discovery only when enabled in compile-time config.
#if ENABLE_HA_DISCOVERY
  TS_PRINTLN("[MQTT] Send Home Assistant discovery config.");
  publishHADiscovery();
#else
  TS_PRINTLN("[MQTT] Home Assistant discovery disabled by ENABLE_HA_DISCOVERY=0");
#endif

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

  snprintf(freqBuffer, sizeof(freqBuffer), "%.6f", FrequencyManager::getTunedFrequency());
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/tuned_frequency", mqttBaseTopic);
  mqtt.publish(topicBuffer, freqBuffer, true);
  delay(5);

  TS_PRINTLN("[MQTT] MQTT config sent");

  // Publish initial Wi-Fi details
  publishWifiDetails();

  // Publish once the meter settings as set in the softeware
  publishMeterSettings();

  // Turn off LED to show everything is setup
  digitalWrite(LED_BUILTIN, HIGH); // turned off

  TS_PRINTLN("[STATUS] Setup done");
  Serial.println("================================\n");

  onScheduled();
}

// ============================================================================
// Frequency Management (MQTT glue over the shared FrequencyManager)
// ============================================================================
//
// The scan / adaptive-tracking / persistence LOGIC lives in the shared
// FrequencyManager (src/services/frequency_manager.cpp) - the SAME code the
// ESPHome build uses. The thin wrappers below only add the MQTT-specific side
// effects (status + offset topics) that FrequencyManager intentionally does not
// know about, so the algorithm stays single-sourced across both builds.

// Status callback handed to FrequencyManager during scans: mirrors scan
// progress to the Home Assistant cc1101_state / status_message topics.
static void mqttFrequencyStatus(const char *state, const char *message)
{
  char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
  if (state && *state)
  {
    snprintf(topicBuffer, sizeof(topicBuffer), "%s/cc1101_state", mqttBaseTopic);
    mqtt.publish(topicBuffer, state, true);
  }
  if (message && *message)
  {
    snprintf(topicBuffer, sizeof(topicBuffer), "%s/status_message", mqttBaseTopic);
    mqtt.publish(topicBuffer, message, true);
  }
}

// Publish the current frequency offset (kHz) to Home Assistant.
static void publishFrequencyOffsetToMqtt()
{
  char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
  char freqBuffer[16];
  snprintf(freqBuffer, sizeof(freqBuffer), "%.3f", FrequencyManager::getOffset() * 1000.0);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/frequency_offset", mqttBaseTopic);
  mqtt.publish(topicBuffer, freqBuffer, true);
}

// Function: performDeepFrequencyScan
// Description: Thin MQTT wrapper over the shared Deep frequency scan. The scan
//              algorithm itself (window mapping, zoom, issue #104 quality guard)
//              lives in FrequencyManager and is shared with the ESPHome build.
//              This wrapper only adds the MQTT-specific side effects (progress
//              status + resulting offset topic).
void performDeepFrequencyScan(float scanRangeMHz, float scanStepMHz)
{
  TS_PRINTLN("[FREQ] [NOTE] Wi-Fi/MQTT connections may temporarily drop and reconnect while the scan is running. This is expected.");

  // FrequencyManager reports the final radio state via mqttFrequencyStatus
  // ("Idle" on success/failure, "Error" if the radio did not respond), so we do
  // NOT re-publish cc1101_state here. Doing so used the boot-time
  // cc1101RadioConnected flag, which is still false during an early auto-scan and
  // would wrongly overwrite the callback's "Idle" with "Not Connected".
  FrequencyManager::performDeepFrequencyScan(scanRangeMHz, scanStepMHz, mqttFrequencyStatus);

  publishFrequencyOffsetToMqtt();
}

// Function: resetFrequencyOffset
// Description: Clears the persisted CC1101 frequency offset back to 0, re-tunes
//              the radio to the configured base frequency and mirrors the reset
//              values to MQTT. This is the MQTT-build equivalent of the ESPHome
//              "Reset Frequency Offset" button and shares FrequencyManager so the
//              storage/re-tune logic stays single-sourced across both targets.
void resetFrequencyOffset()
{
  TS_PRINTLN("[FREQ] Resetting frequency offset to 0");

  // Reset the stored offset to 0 and persist it.
  FrequencyManager::saveFrequencyOffset(0.0);

  // Reset adaptive tracking so a stale correction history cannot immediately
  // re-apply an offset after the reset.
  FrequencyManager::resetAdaptiveTracking();

  // Re-initialize the radio at the base frequency.
  const float baseFrequency = FrequencyManager::getBaseFrequency();
  cc1101RadioConnected = cc1101_init(baseFrequency);
  if (cc1101RadioConnected)
  {
    TS_PRINTF("[FREQ] Radio reinitialized with base frequency: %.6f MHz\n", baseFrequency);
  }
  else
  {
    TS_PRINTF("[FREQ] Radio reinit failed at base frequency: %.6f MHz\n", baseFrequency);
  }

  // Mirror the reset values to Home Assistant.
  publishFrequencyOffsetToMqtt();
  char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
  char freqBuffer[16];
  snprintf(freqBuffer, sizeof(freqBuffer), "%.6f", FrequencyManager::getTunedFrequency());
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/tuned_frequency", mqttBaseTopic);
  mqtt.publish(topicBuffer, freqBuffer, false);
}

// Function: adaptiveFrequencyTracking
// Description: Thin MQTT wrapper over the shared adaptive frequency tracking.
//              Delegates the FREQEST accumulation / correction / persistence and
//              radio re-tune to FrequencyManager (shared with the ESPHome build),
//              then mirrors the resulting offset to MQTT.
void adaptiveFrequencyTracking(int8_t freqest)
{
  // Publish frequency_offset only when the shared tracker actually changed the
  // stored offset, to avoid churning the retained MQTT topic on every read when
  // the frequency is already stable.
  const float offsetBefore = FrequencyManager::getOffset();
  FrequencyManager::adaptiveFrequencyTracking(freqest);
  if (FrequencyManager::getOffset() != offsetBefore)
  {
    publishFrequencyOffsetToMqtt();
  }

  // Always publish tuned_frequency and frequency_estimate after each read so
  // the corresponding HA sensors are not left blank/unknown.
  char topicBuffer[MQTT_TOPIC_BUFFER_SIZE];
  char freqBuffer[16];

  snprintf(freqBuffer, sizeof(freqBuffer), "%.6f", FrequencyManager::getTunedFrequency());
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/tuned_frequency", mqttBaseTopic);
  mqtt.publish(topicBuffer, freqBuffer, false);

  constexpr float FREQEST_TO_KHZ = 1.587f;
  snprintf(freqBuffer, sizeof(freqBuffer), "%.3f", static_cast<float>(freqest) * FREQEST_TO_KHZ);
  snprintf(topicBuffer, sizeof(topicBuffer), "%s/frequency_estimate", mqttBaseTopic);
  mqtt.publish(topicBuffer, freqBuffer, false);
}

/**
 * @brief Validate configuration parameters
 *
 * Performs startup validation of configuration parameters to fail fast
 * on invalid settings. Checks:
 * - METER_CODE (must parse to non-zero serial within 24-bit protocol limit)
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
  uint8_t parsed_year = 0;
  uint32_t parsed_serial = 0;

  Serial.println("\n=== Configuration Validation ===");

  // Validate METER_CODE parse results
  if (!everblu::core::parseMeterCode(METER_CODE, &parsed_year, &parsed_serial))
  {
    TS_PRINTLN("[ERROR] Invalid METER_CODE in private.h");
    Serial.println("       Expected dashed format: \"YY-SSSSSSS\" or \"YY-SSSSSSS-NNN\"");
    Serial.println("       Example: label '16-0039185-107' -> '16-0039185-107'");
    valid = false;
  }
  else
  {
    g_meterYear = parsed_year;
    g_meterSerial = parsed_serial;
    Serial.printf("✓ METER_CODE: %s (year=%d, serial=%lu)\n",
                  METER_CODE, g_meterYear, (unsigned long)g_meterSerial);
  }

// Validate FREQUENCY if defined (should be 300-500 MHz for 433 MHz band)
#ifdef FREQUENCY
  if (FREQUENCY < 300.0 || FREQUENCY > 500.0)
  {
    TS_PRINTF("[ERROR] Invalid FREQUENCY=%.2f MHz (expected 300-500 MHz)\n", FREQUENCY);
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
    TS_PRINTF("[ERROR] Invalid DEFAULT_READING_HOUR_UTC=%d (expected 0-23)\n", DEFAULT_READING_HOUR_UTC);
    valid = false;
  }
  else if (DEFAULT_READING_MINUTE_UTC < 0 || DEFAULT_READING_MINUTE_UTC > 59)
  {
    TS_PRINTF("[ERROR] Invalid DEFAULT_READING_MINUTE_UTC=%d (expected 0-59)\n", DEFAULT_READING_MINUTE_UTC);
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
  TS_PRINTLN("[ERROR] GDO0 pin not defined in private.h");
  valid = false;
#endif

#if defined(GDO2)
  Serial.printf("✓ GDO2 Pin: GPIO %d (TX/RX FIFO threshold - hardware-assisted underflow prevention)\n", GDO2);
#else // DISABLE_GDO2_FIFO_MANAGEMENT
  // src/core/cc1101.cpp emits a compile-time #error when neither GDO2 nor
  // DISABLE_GDO2_FIFO_MANAGEMENT is defined, so reaching here means the opt-out is set.
  Serial.println("  GDO2 Pin: disabled via DISABLE_GDO2_FIFO_MANAGEMENT (legacy SPI polling fallback)");
#endif

  // Validate reading schedule
  if (!isValidReadingSchedule(readingSchedule))
  {
    TS_PRINTF("[WARNING] Invalid reading schedule '%s'. Will fall back to 'Monday-Friday'.\n", readingSchedule);
    Serial.println("         Expected: presets ('Monday-Friday', 'Monday-Saturday', 'Monday-Sunday') or a single day ('Monday'..'Sunday')");
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

  // Parse METER_CODE into g_meterYear, g_meterSerial, and topic buffers
  // Must happen before any code uses these values
  parseMeterCode();

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
  TS_PRINTF("[STATUS] Firmware version: %s\n", EVERBLU_FW_VERSION);
  TS_PRINTF("[STATUS] Target meter: 20%02d-%07lu\n\n", g_meterYear, (unsigned long)g_meterSerial);

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

  Serial.println("✓ Configuration valid - proceeding with initialization\n");

  // Note: mqttBaseTopic and meterSerialStr are initialized at global scope
  // to ensure they're ready when EspMQTTClient constructor runs
  TS_PRINTF("[MQTT] Base topic: %s\n", mqttBaseTopic);
  TS_PRINTF("[MQTT] Meter serial string: %s\n", meterSerialStr);
  TS_PRINTF("[MQTT] mqttBaseTopic length: %d\n", strlen(mqttBaseTopic));

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW); // turned on to start with

  // Initialize persistent storage
#if defined(ESP8266)
  // Clear EEPROM if requested (set CLEAR_EEPROM_ON_BOOT=1 in private.h)
  // Use this when replacing ESP board, CC1101 module, or moving to a different meter.
  // The normal path leaves EEPROM init to FrequencyManager::begin() ->
  // StorageAbstraction::begin(); only the maintenance clear needs an explicit
  // EEPROM.begin() here (avoids initializing EEPROM twice on every boot).
#if CLEAR_EEPROM_ON_BOOT
  EEPROM.begin(EEPROM_SIZE);
  TS_PRINTLN("[STORAGE] CLEARING EEPROM (CLEAR_EEPROM_ON_BOOT = 1)...");
  for (int i = 0; i < EEPROM_SIZE; i++)
  {
    EEPROM.write(i, 0xFF);
  }
  EEPROM.commit();
  TS_PRINTLN("[STORAGE] EEPROM cleared. Remember to set CLEAR_EEPROM_ON_BOOT = 0 after testing!");
#endif
#endif

  // Initialize the shared FrequencyManager: register this build's radio + meter
  // callbacks, then load any persisted offset. This is the SAME implementation the
  // ESPHome build uses (src/services/frequency_manager.cpp), so the scan, adaptive
  // tracking and storage logic is single-sourced across both targets.
  FrequencyManager::setRadioInitCallback(cc1101_init);
  FrequencyManager::setMeterReadCallback(get_meter_data);
  FrequencyManager::setAutoScanEnabled(autoScanEnabled);
  FrequencyManager::setAdaptiveThreshold(ADAPT_THRESHOLD);
  const float loadedOffset = FrequencyManager::begin(FREQUENCY);

  const bool noStoredOffset = (loadedOffset == 0.0f);

  // If no valid frequency offset found and auto-scan is enabled, perform Deep scan.
  // FrequencyManager updates its own stored offset during the scan, so no reload.
  if (noStoredOffset && autoScanEnabled)
  {
    TS_PRINTLN("[FREQ] No stored frequency offset found. Performing Deep frequency scan...");
    performDeepFrequencyScan();
  }
  else if (noStoredOffset)
  {
    TS_PRINTLN("[FREQ] AUTO_SCAN_ENABLED=0; skipping automatic frequency scan (offset remains 0.0 MHz).");
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
  TS_PRINTLN("[WIFI] Wi-Fi PHY mode 11G is disabled.");
#endif
#else
  TS_PRINTLN("[WIFI] Wi-Fi PHY mode setting not applicable on this platform.");
#endif

  // Validate and log the configured reading schedule
  TS_PRINTF("[SCHEDULE] Reading schedule (configured): %s\n", readingSchedule);
  validateReadingSchedule();
  TS_PRINTF("[SCHEDULE] Reading schedule (effective): %s\n", readingSchedule);

  // Log effective frequency and warn if default is used
  TS_PRINTF("[FREQ] Frequency (effective): %.6f MHz\n", (double)FREQUENCY);
#if FREQUENCY_DEFINED_DEFAULT
  TS_PRINTLN("[NOTE] FREQUENCY not set in private.h; using default 433.820000 MHz (RADIAN).");
#endif

  // Optional functionalities of EspMQTTClient
#if ENABLE_MQTT_DEBUGGING
  mqtt.enableDebuggingMessages(true); // Enable debugging messages sent to serial output
  Serial.println(">> MQTT debugging enabled");
#endif

  // Set CC1101 radio frequency with automatic calibration
  TS_PRINTLN("[FREQ] Initializing CC1101 radio...");
  float effectiveFrequency = FrequencyManager::getTunedFrequency();
  if (FrequencyManager::getOffset() != 0.0)
  {
    TS_PRINTF("[FREQ] Applying stored frequency offset: %.6f MHz (effective: %.6f MHz)\n",
                  FrequencyManager::getOffset(), effectiveFrequency);
  }
  if (!cc1101_init(effectiveFrequency))
  {
    TS_PRINTLN("[WARNING] CC1101 radio initialization failed!");
    Serial.println("Please check: 1) Wiring connections 2) 3.3V power supply 3) SPI pins");
    Serial.println("Continuing with WiFi/MQTT only - radio functionality will not be available");
    Serial.println("Device will remain accessible via WiFi/MQTT for diagnostics and configuration");
    cc1101RadioConnected = false;
  }
  else
  {
    TS_PRINTLN("[FREQ] CC1101 radio initialized successfully");
    cc1101RadioConnected = true;
    TS_PRINTF("[FREQ] Adaptive frequency threshold set to %d reads\n", ADAPT_THRESHOLD);
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

  TS_PRINTLN("[NET] Waiting for Wi-Fi/MQTT... timeouts enabled (Wi-Fi 30s, MQTT 30s). Will retry automatically.");
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
    TS_PRINTF("[Wi-Fi] Connected to '%s' (IP: %s, RSSI: %d dBm)\n",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }
  if (mqttUp && !g_prevMqttUp)
  {
    TS_PRINTF("[MQTT] Connected to %s:%d as '%s'\n",
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
      TS_PRINTF("[Wi-Fi] Connecting to '%s'... (status=%d: %s)\n", SECRET_WIFI_SSID, (int)st, wifiStatusToString(st));
      g_lastConnLogMs = millis();
    }

    // If a single attempt seems to stall for too long, force a fresh begin
    if (millis() - g_wifiAttemptStartMs > WIFI_CONNECT_TIMEOUT_MS)
    {
      TS_PRINTLN("[Wi-Fi] Connection attempt timed out. Forcing reconnect...");
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
      TS_PRINTLN("[Wi-Fi] Offline too long. Rebooting device to recover...");
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
      TS_PRINTF("[MQTT] Connecting to %s:%d as '%s'...\n", mqtt.getMqttServerIp(), (int)mqtt.getMqttServerPort(), mqtt.getMqttClientName());
      g_lastConnLogMs = millis();
    }

    if (millis() - g_mqttAttemptStartMs > MQTT_CONNECT_TIMEOUT_MS)
    {
      TS_PRINTLN("[MQTT] Connection attempt seems slow. Will keep retrying in background.");
      // We don't force reconnect here because EspMQTTClient handles the schedule; just reset our timer for logging cadence
      g_mqttAttemptStartMs = millis();
    }

    if (OFFLINE_REBOOT_AFTER_MS > 0 && (millis() - g_mqttOfflineSince) > OFFLINE_REBOOT_AFTER_MS)
    {
      TS_PRINTLN("[MQTT] Offline too long. Rebooting device to recover...");
      delay(200);
      ESP.restart();
    }
    return;
  }
  else
  {
    // Both Wi-Fi and MQTT are up - ensure LED is steady off and reset timers
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
