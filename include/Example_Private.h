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

// Meter-specific configuration
#define METER_YEAR 20        // Last two digits of the year printed on the meter (e.g., 2019 is 19)
#define METER_SERIAL 257000  // Meter Serial Number (omit leading zero)
#define FREQUENCY 433.700007 // Frequency of the meter (discovered via test code)
#define GDO0 5               // Pin on ESP8266 used for GDO0 (General Digital Output 0)
