// WiFi credentials
#define secret_wifi_ssid "xxxx"          // WiFi SSID (Network Name)
#define secret_wifi_password "xxxxxxx"  // WiFi Password

// MQTT server configuration
#define secret_mqtt_server "192.168.xxx.xxx" // MQTT Server IP Address
#define secret_clientName "everbluMeters"   // MQTT Client Name

// MQTT authentication
#define secret_mqtt_username "xxxxxxxxx"        // MQTT Username
#define secret_mqtt_password "xxxxxxxxxxxxxxx"  // MQTT Password

// NTP server for time synchronization
#define secret_local_timeclock_server "pool.ntp.org" // NTP Server Address

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
#define METER_YEAR 21        // Last two digits of the year printed on the meter (e.g., 2019 is 19)
#define METER_SERIAL 260123  // Meter Serial Number (omit leading zero)
// #define FREQUENCY 433.820000 // Meter center frequency in MHz; defaults to 433.82 if not set
// CC1101 GDO0 (data ready) pin connected to your MCU. Choose a valid GPIO for your board.
// Examples:
//  - ESP8266 HUZZAH / D1 mini: GPIO5 (D1)
//  - ESP32 DevKit: GPIO4 or GPIO27 are common choices
#define GDO0 5