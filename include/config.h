// WiFi credentials
#define secret_wifi_ssid "IoT_VLAN"        // WiFi SSID (Network Name)
#define secret_wifi_password "qPGOfpLKI2vRRcOPmLAU8FHC1un7T7R1"  // WiFi Password

// MQTT server configuration
#define secret_mqtt_server "192.168.10.21" // MQTT Server IP Address
#define secret_clientName "EverblueCyble"   // MQTT Client Name

// MQTT authentication
#define secret_mqtt_username "everbluMeters"        // MQTT Username
#define secret_mqtt_password "eshPXjCHd5kvgEdpEbWkvarhx3exvzDj7m"  // MQTT Password

// NTP server for time synchronization
#define secret_local_timeclock_server "192.168.10.21"// NTP Server Address

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
#define METER_YEAR 20        // Last two digits of the year printed on the meter (e.g., 2019 is 19)
#define METER_SERIAL 257750  // Meter Serial Number (omit leading zero)

// Frequency discovered via scan - refined through multiple scans for optimal performance
#define FREQUENCY 433.767029 // Refined scan frequency: 433.767029 MHz (RSSI -85 dBm)
// Previous scan results:
// >>> Initial scan: 433.787018 MHz (RSSI -89 dBm)
// >>> Refined scan: 433.767029 MHz (RSSI -85 dBm) - Current best
// >>> Default RADIAN frequency: 433.820000 MHz

// Clear EEPROM on next boot to force frequency re-discovery
// IMPORTANT: Set to 1 and upload firmware when you:
//  - Replace the ESP8266/ESP32 board
//  - Replace the CC1101 radio module
//  - Move to a different meter
// After one boot with EEPROM cleared, set back to 0 to preserve the discovered frequency.
// The firmware will automatically perform a wide frequency scan and save the optimal frequency.
#define CLEAR_EEPROM_ON_BOOT 0

#define GDO0 5               // Pin on ESP8266 used for GDO0 (General Digital Output 0)







