#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>

#include <arduino.h>


#include <ArduinoOTA.h>
#include "everblu_meters.h"

// Project source : 
// http://www.lamaisonsimon.fr/wiki/doku.php?id=maison2:compteur_d_eau:compteur_d_eau

// Require EspMQTTClient library (by Patrick Lapointe) version 1.13.3
// Install from Arduino library manager (and its dependancies)
// https://github.com/plapointe6/EspMQTTClient/releases/tag/1.13.3
#include "EspMQTTClient.h"

// Edit "everblu_meters.h" file then change the define at the end of the file

#ifndef LED_BUILTIN
// Change this pin if needed
#define LED_BUILTIN 2
#endif

// Note: Libraries are included in "Project Dependencies" file platformio.ini
#include <private.h>               // Passwords etc. not for GitHub

EspMQTTClient mqtt(
    secret_wifi_ssid,     // Your Wifi SSID
    secret_wifi_password, // Your WiFi key
    secret_mqtt_server,   // MQTT Broker server ip
    secret_mqtt_username, // MQTT Username Can be omitted if not needed
    secret_mqtt_password, // MQTT Password Can be omitted if not needed
    secret_clientName,    // MQTT Client name that uniquely identify your device
    1883                  // MQTT Broker server port
);

// char *jsonTemplate = 
// "{                    \
// \"liters\": %d,       \
// \"counter\" : %d,     \
// \"battery\" : %d,     \
// \"timestamp\" : \"%s\"\
// }";

// const char jsonTemplate[] = "{ \"liters\": %d, \"counter\" : %d, \"battery\" : %d, \"timestamp\" : \"%s\" }";

const char jsonTemplate[] = "{ "
                            "\"liters\": %d, "
                            "\"counter\" : %d, "
                            "\"battery\" : %d, "
                            "\"timestamp\" : \"%s\" }";

int _retry = 0;
void onUpdateData()
{
  digitalWrite(LED_BUILTIN, LOW); // turn on LED to show we are getting the data

  struct tmeter_data meter_data;
  meter_data = get_meter_data();

  time_t tnow = time(nullptr);
  struct tm *ptm = gmtime(&tnow);
  Serial.printf("Current date (UTC) : %04d/%02d/%02d %02d:%02d:%02d - %s\n", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec, String(tnow, DEC).c_str());

  char iso8601[128];
  strftime(iso8601, sizeof iso8601, "%FT%TZ", gmtime(&tnow));

  if (meter_data.reads_counter == 0 || meter_data.liters == 0) {
    Serial.println("Unable to retrieve data from meter. Retry later...");

    // Call back this function in 10 sec (in miliseconds)
    if (_retry++ < 10)
      mqtt.executeDelayed(1000 * 10, onUpdateData);

    return;
  }


  Serial.printf("Liters : %d\nBattery (in months) : %d\nCounter : %d\n\n", meter_data.liters, meter_data.battery_left, meter_data.reads_counter);

  mqtt.publish("everblu/cyble/liters", String(meter_data.liters, DEC), true);
  delay(50); // Do not remove
  mqtt.publish("everblu/cyble/counter", String(meter_data.reads_counter, DEC), true);
  delay(50); // Do not remove
  mqtt.publish("everblu/cyble/battery", String(meter_data.battery_left, DEC), true);
  delay(50); // Do not remove
  mqtt.publish("everblu/cyble/timestamp", iso8601, true); // timestamp since epoch in UTC
  delay(50); // Do not remove

  char json[512];
  sprintf(json, jsonTemplate, meter_data.liters, meter_data.reads_counter, meter_data.battery_left, iso8601);
  mqtt.publish("everblu/cyble/json", json, true); // send all data as a json message

  digitalWrite(LED_BUILTIN, HIGH); // turn off the LED now the data has been pulled
}


// This function calls onUpdateData() every days at 10:00am UTC
void onScheduled()
{
  time_t tnow = time(nullptr);
  struct tm *ptm = gmtime(&tnow);


  // At 10:00:00am UTC
  if (ptm->tm_hour == 10 && ptm->tm_min == 0 && ptm->tm_sec == 0) {

    // Call back in 23 hours
    mqtt.executeDelayed(1000 * 60 * 60 * 23, onScheduled);

    Serial.println("It is time to update data from meter :)");

    // Update data
    _retry = 0;
    onUpdateData();

    return;
  }

  // Every 500 ms
  mqtt.executeDelayed(500, onScheduled);
}


String jsonDiscoveryDevice1 =
"{ \
  \"name\": \"Reading (Total)\", \
  \"unique_id\": \"water_meter_value\",\
  \"object_id\": \"water_meter_value\",\
  \"icon\": \"mdi:water\",\
  \"state\": \"{{ states(sensor.water_meter_value)|float / 1 }}\",\
  \"unit_of_measurement\": \"L\",\
  \"device_class\": \"water\",\
  \"state_class\": \"total_increasing\",\
  \"qos\": \"0\",\
  \"state_topic\": \"everblu/cyble/liters\",\
  \"force_update\": \"true\",\
  \"device\" : {\
  \"identifiers\" : [\
  \"14071984\" ],\
  \"name\": \"Water Meter\",\
  \"model\": \"Everblu Cyble ESP8266/ESP32\",\
  \"manufacturer\": \"Psykokwak [Forked by Genestealer]\",\
  \"suggested_area\": \"Home\"}\
}";

String jsonDiscoveryDevice2 = 
"{ \
  \"name\": \"Battery\", \
  \"unique_id\": \"water_meter_battery\",\
  \"object_id\": \"water_meter_battery\",\
  \"device_class\": \"battery\",\
  \"icon\": \"mdi:battery\",\
  \"unit_of_measurement\": \"%\",\
  \"qos\": \"0\",\
  \"state_topic\": \"everblu/cyble/battery\",\
  \"value_template\": \"{{ [(value|int), 100] | min }}\",\
  \"force_update\": \"true\",\
  \"device\" : {\
  \"identifiers\" : [\
  \"14071984\" ],\
  \"name\": \"Water Meter\",\
  \"model\": \"Everblu Cyble ESP8266/ESP32\",\
  \"manufacturer\": \"Psykokwak [Forked by Genestealer]\",\
  \"suggested_area\": \"Home\"}\
}";

String jsonDiscoveryDevice3 =
"{ \
  \"name\": \"Read Counter\", \
  \"unique_id\": \"water_meter_counter\",\
  \"object_id\": \"water_meter_counter\",\
  \"icon\": \"mdi:counter\",\
  \"qos\": \"0\",\
  \"state_topic\": \"everblu/cyble/counter\",\
  \"force_update\": \"true\",\
  \"device\" : {\
  \"identifiers\" : [\
  \"14071984\" ],\
  \"name\": \"Water Meter\",\
  \"model\": \"Everblu Cyble ESP8266/ESP32\",\
  \"manufacturer\": \"Psykokwak [Forked by Genestealer]\",\
  \"suggested_area\": \"Home\"}\
}";

String jsonDiscoveryDevice4 =
  "{ \
  \"name\": \"Timestamp\", \
  \"unique_id\": \"water_meter_timestamp\",\
  \"object_id\": \"water_meter_timestamp\",\
  \"device_class\": \"timestamp\",\
  \"icon\": \"mdi:clock\",\
  \"qos\": \"0\",\
  \"state_topic\": \"everblu/cyble/timestamp\",\
  \"force_update\": \"true\",\
  \"device\" : {\
  \"identifiers\" : [\
  \"14071984\" ],\
  \"name\": \"Water Meter\",\
  \"model\": \"Everblu Cyble ESP8266/ESP32\",\
  \"manufacturer\": \"Psykokwak [Forked by Genestealer]\",\
  \"suggested_area\": \"Home\"}\
}";


String jsonDiscoveryDevice5 =
  "{ \
  \"name\": \"Request Reading Now\", \
  \"unique_id\": \"water_meter_request\",\
  \"object_id\": \"water_meter_request\",\
  \"qos\": \"0\",\
  \"command_topic\": \"everblu/cyble/trigger\",\
  \"payload_press\": \"update\",\
  \"force_update\": \"true\",\
  \"device\" : {\
  \"identifiers\" : [\
  \"14071984\" ],\
  \"name\": \"Water Meter\",\
  \"model\": \"Everblu Cyble ESP8266/ESP32\",\
  \"manufacturer\": \"Psykokwak [Forked by Genestealer]\",\
  \"suggested_area\": \"Home\"}\
}";

void onConnectionEstablished()
{
  Serial.println("Connected to MQTT Broker :)");


  Serial.println("> Configure time from NTP server. Please wait...");
  // Note, my VLAN has no WAN/internet, so I am useing Home Assistant Community Add-on: chrony to proxy the time
  configTzTime("UTC0", secret_local_timeclock_server);

  delay(5000); // Give it a moment for the time to sync the print out the time
  time_t tnow = time(nullptr);
  struct tm *ptm = gmtime(&tnow);
  Serial.printf("Current date (UTC) : %04d/%02d/%02d %02d:%02d:%02d - %s\n", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec, String(tnow, DEC).c_str());
  

  Serial.println("> Configure Arduino OTA flash.");
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    }
    else { // U_FS
      type = "filesystem";
    }
    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd updating.");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("%u%%\r\n", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    }
    else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    }
    else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    }
    else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    }
    else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.setHostname("EVERBLUREADER");
  ArduinoOTA.begin();

  mqtt.subscribe("everblu/cyble/trigger", [](const String& message) {
    if (message.length() > 0) {

      Serial.println("Update data from meter from MQTT trigger");

      _retry = 0;
      onUpdateData();
    }
  });



  Serial.println("> Send MQTT config for HA.");
  // Auto discovery
  delay(50); // Do not remove
  mqtt.publish("homeassistant/sensor/water_meter_value/config", jsonDiscoveryDevice1, true);
  delay(50); // Do not remove
  mqtt.publish("homeassistant/sensor/water_meter_battery/config", jsonDiscoveryDevice2, true);
  delay(50); // Do not remove
  mqtt.publish("homeassistant/sensor/water_meter_counter/config", jsonDiscoveryDevice3, true);
  delay(50); // Do not remove
  mqtt.publish("homeassistant/sensor/water_meter_timestamp/config", jsonDiscoveryDevice4, true);
  delay(50); // Do not remove
  mqtt.publish("homeassistant/button/water_meter_request/config", jsonDiscoveryDevice5, true);
  delay(50); // Do not remove





  // Turn off LED to show everything is setup
  digitalWrite(LED_BUILTIN, HIGH); // turned off

  onScheduled();
}

void setup()
{
  Serial.begin(115200);
  Serial.println("\n");
  Serial.println("Everblu Meters ESP8266 Starting...");
  Serial.println("Water usage data for Home Assistant");
  Serial.println("https://github.com/genestealer/everblu-meters-esp8266-improved");


  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW); // turned on to start with

  mqtt.setMaxPacketSize(1024);
  //mqtt.enableDebuggingMessages(true);

  /*
  // Use this piece of code to find the right frequency.
  for (float i = 433.76f; i < 433.890f; i += 0.0005f) {
    Serial.printf("Test frequency : %f\n", i);
    cc1101_init(i);

    struct tmeter_data meter_data;
    meter_data = get_meter_data();

    if (meter_data.reads_counter != 0 || meter_data.liters != 0) {
      Serial.printf("\n------------------------------\nGot frequency : %f\n------------------------------\n", i);

      Serial.printf("Liters : %d\nBattery (in months) : %d\nCounter : %d\n\n", meter_data.liters, meter_data.battery_left, meter_data.reads_counter);

      digitalWrite(LED_BUILTIN, LOW); // turned on

      while (42);
    }
  }
  */



  cc1101_init(FREQUENCY);

  /*
  // Use this piece of code to test
  struct tmeter_data meter_data;
  meter_data = get_meter_data();
  Serial.printf("\nLiters : %d\nBattery (in months) : %d\nCounter : %d\nTime start : %d\nTime end : %d\n\n", meter_data.liters, meter_data.battery_left, meter_data.reads_counter, meter_data.time_start, meter_data.time_end);
  while (42);
  */

  // Optional functionalities of EspMQTTClient
  mqtt.enableDebuggingMessages(); // Enable debugging messages sent to serial output
  mqtt.enableHTTPWebUpdater(); // Enable the web updater. User and password default to values of MQTTUsername and MQTTPassword. These can be overridded with enableHTTPWebUpdater("user", "password").
  mqtt.enableOTA(); // Enable OTA (Over The Air) updates. Password defaults to MQTTPassword. Port is the default OTA port. Can be overridden with enableOTA("password", port).
  mqtt.enableLastWillMessage("everblu/cyble/lastwill", "I am going offline", true);  // You can activate the retain flag by setting the third parameter to true


}

void loop()
{
  mqtt.loop(); 
  ArduinoOTA.handle();
}
