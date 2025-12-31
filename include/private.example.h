// WiFi credentials
#define secret_wifi_ssid "xxxx"        // WiFi SSID (Network Name)
#define secret_wifi_password "xxxxxxx" // WiFi Password

// MQTT server configuration
#define secret_mqtt_server "192.168.xxx.xxx" // MQTT Server IP Address
#define secret_clientName "everbluMeters"    // MQTT Client Name

// MQTT authentication
#define secret_mqtt_username "xxxxxxxxx"       // MQTT Username
#define secret_mqtt_password "xxxxxxxxxxxxxxx" // MQTT Password

// NTP server for time synchronization
#define secret_local_timeclock_server "pool.ntp.org" // NTP Server Address

// Simple time zone offset (minutes from UTC). Examples:
//  0     = UTC
//  60    = UTC+1
//  -300  = UTC-5
// If omitted, defaults to 0 (UTC).
// #define TIMEZONE_OFFSET_MINUTES 0

// Enable 11G Wi-Fi PHY mode (set to 1 to enable, 0 to disable)
#define ENABLE_WIFI_PHY_MODE_11G 0

// Reading schedule: one of "Monday-Friday", "Monday-Saturday", or "Monday-Sunday"
// If omitted or invalid, the firmware falls back to "Monday-Friday" and logs a warning.
// #define DEFAULT_READING_SCHEDULE "Monday-Friday"

// Optional: daily read time in UTC (hour/minute)
// If omitted, defaults to 10:00 UTC
// #define DEFAULT_READING_HOUR_UTC 10
// #define DEFAULT_READING_MINUTE_UTC 0

// Optional: auto-align reading time to meter wake window
// 1 = enabled (default), 0 = disabled
// #define AUTO_ALIGN_READING_TIME 1
// Strategy: 0 = time_start, 1 = midpoint of [time_start, time_end] (default)
// #define AUTO_ALIGN_USE_MIDPOINT 1

// Meter-specific configuration
// Note: If FREQUENCY is omitted, the firmware defaults to 433.82 MHz (RADIAN).
#define METER_YEAR 21       // Last two digits of the year printed on the meter (e.g., 2019 is 19)
#define METER_SERIAL 260123 // Meter Serial Number (omit leading zero)
// #define FREQUENCY 433.820000 // Meter center frequency in MHz; defaults to 433.82 if not set

// Clear EEPROM on next boot to force frequency re-discovery
// IMPORTANT: Set to 1 and upload firmware when you:
//  - Replace the ESP8266/ESP32 board
//  - Replace the CC1101 radio module
//  - Move to a different meter
// After one boot with EEPROM cleared, set back to 0 to preserve the discovered frequency.
// The firmware will automatically perform a wide frequency scan and save the optimal frequency.
#define CLEAR_EEPROM_ON_BOOT 0

// Optional: disable the automatic wide frequency scan when no offset is stored.
// Leave enabled (default) for the very first boot so the firmware can discover your meter.
// #define AUTO_SCAN_ENABLED 0

// CC1101 GDO0 (data ready) pin connected to your MCU. Choose a valid GPIO for your board.
// Examples:
//  - ESP8266 HUZZAH / D1 mini: GPIO5 (D1)
//  - ESP32 DevKit: GPIO4 or GPIO27 are common choices
#define GDO0 5

// Enable CC1101 / RADIAN protocol debug output printed to serial.
// Set to 1 to enable verbose radio debug messages (helpful for frequency scanning
// and packet debugging). Set to 0 to disable radio debug prints.
// Copy this file to `include/private.h` and change the value as needed.
#define DEBUG_CC1101 0

// Enable MQTT debugging messages printed to serial.
// Set to 1 to enable verbose MQTT connection and message debug output.
// Set to 0 to disable MQTT debug messages (default).
// #define ENABLE_MQTT_DEBUGGING 0