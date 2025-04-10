// WiFi credentials
#define secret_wifi_ssid "xxxx"          // WiFi SSID (network name)
#define secret_wifi_password "xxxxxxx"  // WiFi password

// MQTT server configuration
#define secret_mqtt_server "192.168.xxx.xxx" // MQTT server IP address
#define secret_clientName "everbluMeters"   // MQTT client name

// MQTT authentication
#define secret_mqtt_username "xxxxxxxxx"        // MQTT username
#define secret_mqtt_password "xxxxxxxxxxxxxxx"  // MQTT password

// NTP server for time synchronization
#define secret_local_timeclock_server "pool.ntp.org" // NTP server address

// Meter-specific configuration
#define METER_YEAR 20        // Last two digits of the year printed on the meter (e.g., 2019 is 19)
#define METER_SERIAL 257000  // Serial number printed on the meter
#define FREQUENCY 433.700007 // Frequency of the meter (discovered via test code)
#define GDO0 5               // Pin on ESP8266 used for GDO0 (General Digital Output 0)
