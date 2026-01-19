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
#define SECRET_MQTT_CLIENT_ID "everbluMeters" // Client identifier (meter serial is appended automatically)

// IMPORTANT: Multiple devices running this firmware will append their meter serial number
// to the client ID to ensure uniqueness. For example, if METER_SERIAL is 123456:
//   Final MQTT Client ID: "everbluMeters-123456"
// This prevents MQTT connection conflicts and ensures proper Home Assistant availability tracking
// when multiple meters are connected to the same broker.
// Do NOT manually add the serial to this value - it's done automatically at compile time.

// Authentication
#define SECRET_MQTT_USERNAME "xxxxxxxxx"
#define SECRET_MQTT_PASSWORD "xxxxxxxxxxxxxxx"

// Debugging
// 1 = enable verbose MQTT output on serial
// 0 = disable (default)
#define ENABLE_MQTT_DEBUGGING 0

// Meter number prefix in entity IDs
// Controls whether the meter serial number is included as a prefix in MQTT entity IDs
// and Home Assistant entity names. This is useful for distinguishing entities when
// running multiple meters on the same broker.
//
// 1 (default): Include meter serial as prefix (e.g., "123456_everblu_meter_value")
//              Use this for multiple meters or to preserve multi-meter MQTT history
// 0:           Omit meter serial prefix (e.g., "everblu_meter_value")
//              Use this for single meters where you want to keep existing Home Assistant history
//
#define ENABLE_METER_PREFIX_IN_ENTITY_IDS 1

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
// "gas"   = gas meter - internally stored in the meter's native format (like water)
//           and converted to cubic meters (m³) for display and MQTT, device class gas
#define METER_TYPE "water"

// Gas meter volume divisor (only used when METER_TYPE is "gas")
//
// The RADIAN protocol transmits meter readings in liters (L). For gas meters,
// this divisor converts liters to cubic meters (m³).
//
// Default: 100 (equivalent to 0.01 m³ per unit)
//
// If your readings seem incorrect, verify your meter's pulse weight and adjust
// this divisor accordingly:
//   - 100: 0.01 m³ per unit (typical for modern gas meters)
//   - 1000: 0.001 m³ per unit (0.1 L per unit)
//
#define GAS_VOLUME_DIVISOR 100

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

// Adaptive frequency tracking threshold
// Controls how many successful meter reads trigger an automatic frequency adjustment
// based on the CC1101's FREQEST register (frequency error estimate)
//
// Values:
//   1 (default):  Adjust frequency after every successful read
//                 Best for infrequent readings (once-per-day) or drifting frequencies
//   5:            Adjust after 5 successful reads
//                 Good balance for detecting drift vs avoiding noise
//   10 or higher: Adjust only after many reads
//                 Best for stable, frequent readings (multiple per hour)
//
// The adjustment only occurs if the average frequency error exceeds 2 kHz
// and applies 50% of the measured error to avoid over-correction.
#define ADAPTIVE_THRESHOLD 1

// ============================================================================
// WIFI SERIAL MONITOR CONFIGURATION
// ============================================================================

// Enable WiFi-based serial monitor for remote debugging via Telnet
// WARNING: This exposes all serial output (including credentials and internal state)
// to any device on your local network. Only enable if needed for debugging.
//
// 0 (default): Disabled - improves security and reduces overhead
// 1:           Enabled - allows remote monitoring via telnet on port 23
//
// To use when enabled:
//   telnet <device-ip> 23
#define WIFI_SERIAL_MONITOR_ENABLED 0
#if WIFI_SERIAL_MONITOR_ENABLED
#warning "WiFi serial monitor is ENABLED: this may expose credentials and internal state over the network"
#endif
