# everblu-meters-esp8266/esp32 - Water Usage Data for Home Assistant

Fetch water/gas usage data from Itron EverBlu Cyble Enhanced RF water meters using the RADIAN protocol on 433 MHz with an ESP32/ESP8266 and CC1101 transceiver. Integrated with Home Assistant via MQTT AutoDiscovery.

![Home Assistant MQTT autodiscovery](MQTT_HASS.jpg)

The original software (and much of the hard work to get things functioning) was initially done [here](http://www.lamaisonsimon.fr/wiki/doku.php?id=maison2:compteur_d_eau:compteur_d_eau), then published on GitHub by @neutrinus [here](https://github.com/neutrinus/everblu-meters), and later forked by [psykokwak](https://github.com/psykokwak-com/everblu-meters-esp8266).

Meters supported:

- [Itron EverBlu Cyble Enhanced](https://multipartirtaanugra.com/wp-content/uploads/2020/09/09.-Cyble-RF.pdf)

![Itron EverBlu Cyble Enhanced](meter.jpg)

## Hardware

The project runs on ESP8266/ESP32 with an RF transceiver (CC1101). The hardware can be any ESP32+CC1101 with the correct wiring.
![ESP8266 with CC1101](board2.jpg)
![ESP8266 with CC1101](board.jpg)

### Connections (ESP32/ESP8266 to CC1101)

- Refer to `cc1101.ccp` for SPI pin mappings.
- Refer to `everblu_meters.h` for GDOx pin mappings.

Pin wiring for the [Wemos D1 board](https://www.wemos.cc/en/latest/d1/index.html) and [Adafruit Feather HUZZAH ESP8266](https://learn.adafruit.com/adafruit-feather-huzzah-esp8266/pinouts):

| **CC1101**  | **Wemos** | **HUZZAH ESP8266** | **Notes**                                      |
|-------------|-----------|--------------------|------------------------------------------------|
| VCC         | 3V3       | 3V                 | Connect to the 3.3V power pin.                |
| GDO0        | D1        | GPIO5              | General-purpose digital output.               |
| CSN         | D8        | GPIO15             | SPI chip select; Feather uses GPIO15 as CS.   |
| SCK         | D5        | GPIO14             | SPI clock pin; SPI SCK maps to GPIO14.        |
| MOSI        | D7        | GPIO13             | SPI MOSI pin; maps to GPIO13 on the Feather.  |
| GDO1 (MISO) | D6        | GPIO12             | SPI MISO pin; maps to GPIO12 on the Feather.  |
| GDO2        | D2        | GPIO4              | Another general-purpose digital output.       |
| GND         | G         | GND                | Connect to ground.                            |

### CC1101

Some modules are not labelled on the PCB. Below is the pinout for one:
![CC1101 pinout diagram](cc1101-mapping.png)
![CC1101 example](cc1101.jpg)

## Configuration

1. Download [Visual Studio Code](https://code.visualstudio.com/).
2. Install [PlatformIO for VS Code](https://platformio.org/) (this will install all required dependencies and may require a VS Code restart).
3. Copy `Exampleprivate.h` into the `src` folder and rename it to `private.h`.
   - Update Wi-Fi and MQTT details in `private.h`. If you do not use a username and password for MQTT, comment those lines out with `//`.
   - Set the meter serial number (without the leading 0) and production year in `private.h`. This information can be found on the meter label itself:
![Cyble Meter Label](meter_label.png)(meter_label_21.png)
4. Update `platformio.ini` to match your specific platform and board.
5. For the first-time setup only: towards the bottom of `everblu-meters-esp8266.cpp`, uncomment the Frequency Discovery code snippet to display debug output of your meter's discovered frequency value. Once you have this, update the `FREQUENCY` value in `private.h` and re-comment this code. For best results, perform this process during local business hours. For more information, see the section on Frequency Adjustment below.
6. Compile and flash the code to your ESP device, keeping it connected to your computer.
   - Use PlatformIO > Upload and Monitor for the first-time Frequency Discovery process. Use PlatformIO > Upload if you already have your frequency information or are just updating the build.
7. After a few seconds, your meter data should appear in the bottom panel (terminal), and data should be pushed to MQTT.
   - If you have set up Frequency Discovery, you should also see this process output at this point.
8. The device will query the meter once a day (every 24 hours) and retry every hour if the query fails.

## Troubleshooting

### Frequency Adjustment

Your transceiver module may not be calibrated correctly. Adjust the frequency slightly lower or higher and try again. You may use an RTL-SDR to measure the required offset. Uncomment the relevant part of the code in the `everblu-meters-esp8266.cpp` file to scan all frequencies around the meter's frequency to find the correct one.

```cpp
  /*
  Serial.printf("###### FREQUENCY DISCOVERY ENABLED ######\nStarting Frequency Scan...\n");
  for (float i = 433.76f; i < 433.890f; i += 0.0005f) {
    Serial.printf("Test frequency : %f\n", i);
    cc1101_init(i);
    struct tmeter_data meter_data;
    meter_data = get_meter_data();
    if (meter_data.reads_counter != 0 || meter_data.liters != 0) {
      Serial.printf("\n------------------------------\nGot frequency : %f\n------------------------------\n", i);
      Serial.printf("Litres : %d\nBattery (in months) : %d\nCounter : %d\n\n", meter_data.liters, meter_data.battery_left, meter_data.reads_counter);
      digitalWrite(LED_BUILTIN, LOW); // turned on
      while (42);
    }
  }
    Serial.printf("###### FREQUENCY DISCOVERY FINISHED ######\nOnce you have discovered the correct frequency you can disable this scan.\n\n");
  */
```

### Business Hours

> [!TIP]
> Your meter may be configured to listen for requests only during business hours to conserve energy. If you are unable to communicate with the meter, try again during business hours (8:00–16:00), Monday to Friday. As a rule of thumb, set up your device during business hours to avoid confusion and unnecessary troubleshooting.
> [!NOTE]
> This is particularly relevant in the UK.

### Serial Number Starting with 0

Ignore the leading 0 and provide the serial number in the configuration without it.

### Distance Between Device and Meter

Typically, a CC1101 433 MHz module with an external wire coil antenna has a maximum range of 300–500 m. SMA CC1101 boards with high-gain antennas may increase or even double this range. However, be mindful of the distance for effective use.

## Origin and Licence

This code is based on code from [La Maison Simon](http://www.lamaisonsimon.fr/wiki/doku.php?id=maison2:compteur_d_eau:compteur_d_eau).

The licence is unknown. According to one of the authors (Fred):

> I didn't put a licence on this code; maybe I should. I didn't know much about licensing at the time.
> This code was created by "looking" at the RADIAN protocol, which is said to be open source earlier in the page. I don't know if that helps.
