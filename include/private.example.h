// ============================================================================
// NETWORK CONFIGURATION
// ============================================================================

// Wi-Fi credentials
#define SECRET_WIFI_SSID "xxxx"        // Wi-Fi SSID (network name)
#define SECRET_WIFI_PASSWORD "xxxxxxx" // Wi-Fi password

// Optional: force 802.11g PHY mode
// 1 = enable 11g mode
// 0 = use default PHY selection
#define ENABLE_WIFI_PHY_MODE_11G 0

// ============================================================================
// MQTT CONFIGURATION
// ============================================================================

// Broker connection
#define SECRET_MQTT_SERVER "192.168.xxx.xxx"  // Broker IP or hostname
#define SECRET_MQTT_CLIENT_ID "everbluMeters" // Client identifier

// Authentication
#define SECRET_MQTT_USERNAME "xxxxxxxxx"
#define SECRET_MQTT_PASSWORD "xxxxxxxxxxxxxxx"

// Debugging
// 1 = enable verbose MQTT output on serial
// 0 = disable (default)
#define ENABLE_MQTT_DEBUGGING 0

// ============================================================================
// TIME AND SCHEDULING
// ============================================================================

// NTP server for time synchronisation
#define SECRET_NTP_SERVER "pool.ntp.org"

// Time zone offset in minutes relative to UTC
// Examples: 0 = UTC, 60 = UTC+1, -300 = UTC-5
#define TIMEZONE_OFFSET_MINUTES 0

// Reading schedule
// Supported values:
//   "Monday-Friday"
//   "Monday-Saturday"
//   "Monday-Sunday"
// Invalid or missing values fall back to "Monday-Friday".
#define DEFAULT_READING_SCHEDULE "Monday-Friday"

// Default daily read time (UTC)
#define DEFAULT_READING_HOUR_UTC 10
#define DEFAULT_READING_MINUTE_UTC 0

// Automatically align reading time to meter wake window
// 1 = enabled (recommended)
// 0 = disabled
#define AUTO_ALIGN_READING_TIME 1

// Alignment strategy when auto-alignment is enabled
// 0 = align to time_start
// 1 = align to midpoint of [time_start, time_end]
#define AUTO_ALIGN_USE_MIDPOINT 0

// ============================================================================
// METER IDENTIFICATION
// ============================================================================
//
// Serial format: XX-YYYYYYY-ZZZ
//   - Use the 2-digit year (XX)
//   - Use the middle section only (YYYYYYY)
//   - Ignore the suffix (-ZZZ)
//
// Example:
//   Serial: 20-02777550-234
//     METER_YEAR   = 20
//     METER_SERIAL = 2777550
//
// Common mistakes:
//   - Using a 4-digit year
//   - Including the suffix
//   - Keeping leading zeros in the serial
//
#define METER_YEAR xx
#define METER_SERIAL xxxxxxx

// Meter type configuration
// "water" = water meter (default) - readings in liters (L), device class water
// "gas"   = gas meter - readings in cubic meters (m³), device class gas
#define METER_TYPE "water"

// ============================================================================
// RADIO / CC1101 CONFIGURATION
// ============================================================================

// Optional: manually set meter centre frequency (MHz)
// Defaults to 433.82 MHz if not defined
// #define FREQUENCY                 433.820000

// Optional: clear EEPROM on next boot to force frequency rediscovery
//
// Set to 1 for a single boot if you:
//   - Replace the ESP8266 / ESP32
//   - Replace the CC1101 module
//   - Move to a different meter
//
// After one successful boot, set back to 0 to retain the stored frequency.
#define CLEAR_EEPROM_ON_BOOT 0

// Enable wide frequency scan when no stored offset exists
//
// 1 (default): Perform ~2 minute scan before MQTT connection
// 0:           Skip scan even if no offset is stored
//
// To re-run the scan, set CLEAR_EEPROM_ON_BOOT to 1 or re-enable this.
#define AUTO_SCAN_ENABLED 1

// CC1101 GDO0 (data-ready) pin assignment
// ESP8266 (D1 mini / HUZZAH): GPIO5 (D1)
// ESP32 DevKit: GPIO4 or GPIO27
#define GDO0 5

// Radio protocol debug output
// 1 = enable verbose CC1101 / RADIAN logging
// 0 = disable (default)
#define DEBUG_CC1101 0

// ============================================================================
// READING RETRY CONFIGURATION
// ============================================================================

// Maximum number of retry attempts when reading fails
// After this many failed attempts, the system enters a 1-hour cooldown period
// Default: 10 retries
#define MAX_RETRIES 10
