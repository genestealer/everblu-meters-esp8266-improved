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

// Meter-specific configuration
#define METER_YEAR 20        // Last two digits of the year printed on the meter (e.g., 2019 is 19)
#define METER_SERIAL 257750  // Meter Serial Number (omit leading zero)
// #define FREQUENCY 433.820000 // Meter center frequency in MHz; defaults to 433.82 if not set

// #define FREQUENCY 433.763031 // Frequency of the meter (discovered via test code)
// >>> Found meter at frequency: 433.800049 MHz
// >>> Fine scan chose frequency: 433.787018 MHz (RSSI -89 dBm)
#define GDO0 5               // Pin on ESP8266 used for GDO0 (General Digital Output 0)







