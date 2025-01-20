
# everblu-meters - Water usage data for Home Assistant
Fetch water/gas usage data from Cyble EverBlu meters using RADIAN protocol on 433Mhz. Intergrated with Home Assistant via MQTT. 

Note: Home Assistant MQTT autodiscovery is working.

![Home Assistant MQTT autodiscovery](MQTT_HASS.jpg)

Meters supported:
- Itron EverBlu Cyble Enhanced

![Itron EverBlu Cyble Enhanced](meter.jpg)

## Hardware
![ESP8266 with CC1101](board2.jpg)
![ESP8266 with CC1101](board.jpg)

The project runs on ESP8266/ESP32 with an RF transreciver (CC1101). 

### Connections (ESP32/ESP8266 to CC1101):
- See `cc1101.ccp` for SPI pins mapping.
- See `everblu_meters.h` for GDOx pins mapping.

Pins wiring for [Wemos D1 board](https://www.wemos.cc/en/latest/d1/index.html) and [Adafruit Feather HUZZAH ESP8266](https://www.wemos.cc/en/latest/d1/index.html](https://learn.adafruit.com/adafruit-feather-huzzah-esp8266/pinouts)):

| **CC1101**  | **Wemos** | **HUZZAH ESP8266** | **Notes**                                      |
|-------------|-----------|---------------------------|------------------------------------------------|
| VCC         | 3V3       | 3V                       | Connect to the 3.3V power pin.                |
| GDO0        | D1        | GPIO5                    | General-purpose digital output.               |
| CSN         | D8        | GPIO15                   | SPI chip select; Feather has GPIO15 as CS.    |
| SCK         | D5        | GPIO14                   | SPI clock pin; SPI SCK maps to GPIO14.        |
| MOSI        | D7        | GPIO13                   | SPI MOSI pin; maps to GPIO13 on the Feather.  |
| GDO1 (MISO) | D6        | GPIO12                   | SPI MISO pin; maps to GPIO12 on the Feather.  |
| GDO2        | D2        | GPIO4                    | Another general-purpose digital output.       |
| GND         | G         | GND                      | Connect to ground.                            |


## Configuration
1. Install EspMQTTClient through Arduino library manager as it required for MQTT
2. Update WiFi and MQTT details in everblu-meters-esp8266.ino, if you do not use username and password for MQTT then comment those out with //
3. Set meter serial number (without the leading 0) and production year in `everblu_meters.h` (at the end of the file), it can be found on the meter label itself:
![Cyble Meter Label](meter_label.png)
4. Flash the sketch to your ESP device
5. After a few second your meter data should be on the screen (serial console) and data should be pushed to MQTT.
6. The device will query the meter once a day, every 24 hours and retry every hour if query failed.

## Troubleshooting

### Frequency adjustment
Your transreciver module may be not calibrated correctly, please modify frequency a bit lower or higher and try again. You may use RTL-SDR to measure the offset needed.
You can uncomment the part of the code in the `everblu-meters-esp8266.cpp` file that scans all the frequencies around the meter frequency to find the correct one.

```
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
```


### Business hours
Your meter may be configured in such a way that is listens for request only during hours when data collectors work - to conserve energy. If you are unable to communicate with the meter, please try again during business hours (8:00-16:00), Monday to Friday. As a rule of thumb, please try to set up your device only during business hours, then you can avoid confusion and asking questions why is it not working!  

### Serial number starting with 0
Please ignore the leading 0, provide serial in configuration without it.


## Origin and license

This code is based on code from http://www.lamaisonsimon.fr/wiki/doku.php?id=maison2:compteur_d_eau:compteur_d_eau 


The license is unknown, citing one of the authors (fred):

> I didn't put a license on this code maybe I should, I didn't know much about it in terms of licensing.
> this code was made by "looking" at the radian protocol which is said to be open source earlier in the page, I don't know if that helps?
