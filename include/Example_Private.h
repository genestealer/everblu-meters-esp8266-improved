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

// Enable or disable meter frequency discovery scan process (set to 1 to enable, 0 to disable)
// Only needed to find the frequency of the meter, disable after finding it
#define SCAN_FREQUENCY_433MHZ 0

// Meter-specific configuration
#define METER_YEAR 21        // Last two digits of the year printed on the meter (e.g., 2019 is 19)
#define METER_SERIAL 260123  // Meter Serial Number (omit leading zero)
#define FREQUENCY 433.700007 // Frequency of the meter (discovered via test code)
#define GDO0 5               // Pin on ESP8266 used for GDO0 (General Digital Output 0)