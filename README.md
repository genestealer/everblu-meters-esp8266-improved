# everblu-meters-esp8266/esp32 - Itron EverBlu Cyble Enhanced RF RADIAN Water Usage Data for Home Assistant

[![ESP8266 Build](https://github.com/genestealer/everblu-meters-esp8266-improved/actions/workflows/build-esp8266.yml/badge.svg?branch=main)](https://github.com/genestealer/everblu-meters-esp8266-improved/actions/workflows/build-esp8266.yml)
[![ESP32 Build](https://github.com/genestealer/everblu-meters-esp8266-improved/actions/workflows/build-esp32.yml/badge.svg?branch=main)](https://github.com/genestealer/everblu-meters-esp8266-improved/actions/workflows/build-esp32.yml)
[![Code Quality](https://github.com/genestealer/everblu-meters-esp8266-improved/actions/workflows/code-quality.yml/badge.svg?branch=main)](https://github.com/genestealer/everblu-meters-esp8266-improved/actions/workflows/code-quality.yml)

[![Made with PlatformIO](https://img.shields.io/badge/Made%20with-PlatformIO-orange?logo=platformio)](https://platformio.org)
[![ESP8266](https://img.shields.io/badge/ESP-8266-blue?logo=espressif)](https://www.espressif.com/en/products/socs/esp8266)
[![ESP32](https://img.shields.io/badge/ESP-32-blue?logo=espressif)](https://www.espressif.com/en/products/socs/esp32)
[![WiFi](https://img.shields.io/badge/WiFi-Ready-green?logo=wifi)](https://en.wikipedia.org/wiki/Wi-Fi)
[![OTA Updates](https://img.shields.io/badge/OTA-Supported-brightgreen?logo=arduino)](https://arduino-esp8266.readthedocs.io/en/latest/ota_updates/readme.html)
[![MQTT](https://img.shields.io/badge/MQTT-Compatible-purple?logo=mqtt)](https://mqtt.org)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-41BDF5?logo=homeassistant)](https://www.home-assistant.io)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)

Fetch water or gas usage data from Itron EverBlu Cyble Enhanced RF water meters using the RADIAN protocol (Sontex, Itron) on 433 MHz with an ESP32/ESP8266 and CC1101 transceiver. Integrated with Home Assistant via MQTT AutoDiscovery.

According to the radio communication approval paperwork, this may also work with the following models, though they are untested:

- AnyQuest Cyble Enhanced
- EverBlu Cyble
- AnyQuest Cyble Basic

![Home Assistant MQTT autodiscovery](docs/images/MQTT_HASS2.jpg)

The original software (and much of the foundational work) was initially developed [here](http://www.lamaisonsimon.fr/wiki/doku.php?id=maison2:compteur_d_eau:compteur_d_eau), later published on GitHub by @neutrinus [here](https://github.com/neutrinus/everblu-meters), and subsequently forked by [psykokwak](https://github.com/psykokwak-com/everblu-meters-esp8266).

Supported meters:

- [Itron EverBlu Cyble Enhanced](https://multipartirtaanugra.com/wp-content/uploads/2020/09/09.-Cyble-RF.pdf)

![Itron EverBlu Cyble Enhanced](docs/images/meter.jpg)

## Features

- Fetch water or gas usage data from Itron EverBlu Cyble Enhanced RF water meters.
- Includes RSSI (Radio Signal Strength Indicator), LQI (Link Quality) and Signal Strength for the meter for diagnostics.
- Time Start and Time End sensors to indicate when the meter wakes and sleeps.
- MQTT integration for Home Assistant with AutoDiscovery.
- Automatic CC1101 frequency calibration with manual fallback.
- Wi-Fi diagnostics and OTA updates.
- Built-in CRC-16/KERMIT verification to discard corrupted RADIAN frames before publishing data.
- Reading Schedule Configuration: Configure the days when the meter should be queried (e.g., Monday-Friday, Monday-Saturday, or Monday-Sunday).
- Daily scheduled meter readings.

---

### Credits

This project builds on reverse engineering efforts by:

- La Maison Simon (http://www.lamaisonsimon.fr/)
- @neutrinus and @psykokwak on GitHub

Their original projects did not include an open-source license. If you reuse or modify their specific code portions, please review their repositories and respect any stated limitations or intentions.

---

## Hardware

The project runs on ESP8266/ESP32 with an RF transceiver (CC1101). The hardware can be any ESP32+CC1101 with the correct wiring.
![ESP8266 with CC1101](docs/images/board2.jpg)
![ESP8266 with CC1101](docs/images/board.jpg)

### Connections (ESP32/ESP8266 to CC1101)

The project uses the ESP8266/ESP32's **hardware SPI pins** to communicate with the CC1101 radio module. Below are the wiring diagrams for common ESP8266 boards and ESP32 DevKit.

#### Pin Mapping Reference

**For ESP8266 (All Boards):**
- **SCK (SPI Clock)**: GPIO 14
- **MISO (Master In Slave Out)**: GPIO 12
- **MOSI (Master Out Slave In)**: GPIO 13
- **CS/SS (Chip Select)**: GPIO 15
- **GDO0 (CC1101 Data Ready)**: GPIO 5 (configurable in `private.h`)

#### Wiring Table

Pin wiring for the [Wemos D1 Mini](https://www.wemos.cc/en/latest/d1/index.html), [Adafruit Feather HUZZAH ESP8266](https://learn.adafruit.com/adafruit-feather-huzzah-esp8266/pinouts), and ESP32 DevKit:

| **CC1101 Pin** | **Function** | **ESP8266 GPIO** | **Wemos D1 Mini** | **HUZZAH ESP8266** | **ESP32 GPIO** | **ESP32 DevKit** | **Notes** |
|----------------|--------------|------------------|-------------------|--------------------|----------------|------------------|-----------|
| **VCC**        | Power        | 3.3V             | 3V3               | 3V                 | 3.3V           | 3V3              | **Important:** Use 3.3V only! |
| **GND**        | Ground       | GND              | G                 | GND                | GND            | GND              | Common ground |
| **SCK**        | SPI Clock    | GPIO 14          | D5                | #14                | GPIO 18        | SCK              | Hardware SPI clock |
| **MISO**       | SPI Data In  | GPIO 12          | D6                | #12                | GPIO 19        | MISO             | Also labeled as GDO1 on some CC1101 modules |
| **MOSI**       | SPI Data Out | GPIO 13          | D7                | #13                | GPIO 23        | MOSI             | Hardware SPI MOSI |
| **CSN/CS**     | Chip Select  | GPIO 15          | D8                | #15                | GPIO 5         | SS               | SPI chip select |
| **GDO0**       | Data Ready   | GPIO 5           | D1                | #5                 | GPIO 4         | GPIO 4           | Digital interrupt pin (configurable in `private.h`) |
| **GDO2**       | Not used     | -                | -                 | -                  | -              | -                | Leave disconnected (optional) |

#### Important Notes

- **Voltage:** The CC1101 operates at **3.3V only**. Do not connect to 5V or you will damage the module.
- **Hardware SPI:** This project uses the ESP8266/ESP32's hardware SPI interface for reliable, high-speed communication.
- **GDO0 Pin:** Default is GPIO 5 for ESP8266, GPIO 4 for ESP32. You can change this in your `private.h` file if needed.
- **Wemos D1 Mini Labels:** The Wemos board uses "D" labels (D1, D5, D6, etc.) which correspond to specific GPIO numbers. See the table above for the mapping.
- **HUZZAH Pinout:** The Adafruit HUZZAH uses GPIO numbers directly on the silkscreen (no "D" labels). The table shows the corresponding GPIO numbers.
- **ESP32 DevKit:** Most ESP32 DevKit boards use standard hardware SPI pins (SCK=18, MISO=19, MOSI=23, CS=5). The silkscreen typically labels these with their function names.

#### Regulatory Notes

The EU declaration of conformity for the Cyble RF family (Cyble NRF, Cyble NRF HT, Cyble OMS wM-Bus 434) specifies operation in the 434 MHz ISM band with ≤ 10 mW radiated power. The CC1101 settings used in this project stay within that envelope, but ensure your antenna choice and deployment follow local regulations.

#### Quick Reference: Wemos D1 Mini

```text
CC1101 → Wemos D1 Mini
VCC    → 3V3
GND    → G
SCK    → D5 (GPIO 14)
MISO   → D6 (GPIO 12)
MOSI   → D7 (GPIO 13)
CSN    → D8 (GPIO 15)
GDO0   → D1 (GPIO 5)
```

#### Quick Reference: Adafruit Feather HUZZAH ESP8266

```text
CC1101 → HUZZAH ESP8266
VCC    → 3V
GND    → GND
SCK    → #14 (GPIO 14)
MISO   → #12 (GPIO 12)
MOSI   → #13 (GPIO 13)
CSN    → #15 (GPIO 15)
GDO0   → #5  (GPIO 5)
```

#### Quick Reference: ESP32 DevKit (esp32dev)

```text
CC1101 → ESP32 DevKit
VCC    → 3V3
GND    → GND
SCK    → SCK (GPIO 18 on most DevKit boards)
MISO   → MISO (GPIO 19)
MOSI   → MOSI (GPIO 23)
CSN    → SS (GPIO 5 by default on many boards)
GDO0   → GPIO 4 (or GPIO 27)  ← set this in include/private.h as GDO0
```

Notes for ESP32
- Use the board’s hardware SPI pins (SCK/MISO/MOSI/SS). The defaults are provided by the Arduino core and used automatically by this project.
- Choose a free GPIO for GDO0 (e.g., 4 or 27) and set `#define GDO0 <pin>` in `include/private.h`.
- Power the CC1101 from 3.3V only.

#### Adafruit Feather HUZZAH Silkscreen Labels

To make wiring dead-simple on the HUZZAH, here’s the exact silkscreen text next to each pin we use and what it connects to on the CC1101:

- Power
  - Board label: "3V" → CC1101 VCC (3.3V only)
  - Board label: "GND" → CC1101 GND

- SPI signals
  - Board label: "SCK / #14" → CC1101 SCK (SPI Clock)
  - Board label: "MISO / #12" → CC1101 MISO
  - Board label: "MOSI / #13" → CC1101 MOSI
  - Board label: "SS / #15"   → CC1101 CSN (Chip Select)

- CC1101 interrupt (data ready)
  - Board label: "#5" → CC1101 GDO0  (default; configurable via `private.h`)

Notes
- On the HUZZAH, many pins show both the function and the GPIO number, e.g. "SCK / #14". You can use either reference when wiring.
- Only use the 3V (3.3V) pin to power the CC1101. Do not use 5V.

#### HUZZAH Boot-Strap Pins and Red LED (GPIO #0)

On the Adafruit Feather HUZZAH ESP8266, **GPIO #0 has a red LED attached and is also a boot-strap pin** used to enter the ROM bootloader. Important implications:

- If GPIO #0 is held LOW during reset/power-up, the ESP8266 will enter the bootloader instead of running your sketch.
- The red LED on GPIO #0 is wired “reverse”: writing LOW turns the LED ON, writing HIGH turns it OFF.
- GPIO #0 does not have an internal pull-up by default.

Because of the above, **do not use GPIO #0 for CC1101 GDO0**. This project defaults to using **GPIO #5** for GDO0 on HUZZAH, which is safe and avoids accidental bootloader entry. You can still use GPIO #0 for simple LED indication in your own code, but avoid wiring CC1101 signals to it.

Also note other ESP8266 boot-strap pins on HUZZAH:
- GPIO #15 (used here as CS/SS) must be LOW at boot (the HUZZAH board provides the correct pull-down). Don’t force it HIGH during reset.
- GPIO #2 should normally be HIGH at boot (not used by this project).

### CC1101

Some modules are not labeled on the PCB. Below is the pinout for one:
![CC1101 pinout diagram](docs/images/cc1101-mapping.png)
![CC1101 example](docs/images/cc1101.jpg)

---

## MQTT Integration

The following MQTT topics are used to integrate the device with Home Assistant via AutoDiscovery:

| **Sensor**          | **MQTT Topic**                  | **Description**                                                                |
|---------------------|---------------------------------|--------------------------------------------------------------------------------|
| `Liters`            | `everblu/cyble/liters`          | Total water usage in liters.                                                   |
| `Battery`           | `everblu/cyble/battery`         | Remaining battery life in months.                                              |
| `Counter`           | `everblu/cyble/counter`         | Number of times the meter has been read (wraps around 255→1).                  |
| `RSSI`              | `everblu/cyble/rssi`            | Raw RSSI value of the meter's signal.                                          |
| `RSSI (dBm)`        | `everblu/cyble/rssi_dbm`        | RSSI value converted to dBm.                                                   |
| `RSSI (%)`          | `everblu/cyble/rssi_percentage` | RSSI value converted to a percentage.                                          |
| `Time Start`        | `everblu/cyble/time_start`      | Time when the meter wakes up, formatted as `HH:MM`.                            |
| `Time End`          | `everblu/cyble/time_end`        | Time when the meter goes to sleep, formatted as `HH:MM`.                       |
| `Timestamp`         | `everblu/cyble/timestamp`       | ISO 8601 timestamp of the last reading.                                        |
| `Wi-Fi IP`          | `everblu/cyble/wifi_ip`         | IP address of the device.                                                      |
| `Wi-Fi RSSI`        | `everblu/cyble/wifi_rssi`       | Wi-Fi signal strength in dBm.                                                  |
| `Wi-Fi Signal (%)`  | `everblu/cyble/wifi_signal_percentage` | Wi-Fi signal strength as a percentage.                                  |
| `MAC Address`       | `everblu/cyble/mac_address`     | MAC address of the device.                                                     |
| `SSID`              | `everblu/cyble/ssid`            | Wi-Fi SSID the device is connected to.                                         |
| `BSSID`             | `everblu/cyble/bssid`           | Wi-Fi BSSID the device is connected to.                                        |
| `Uptime`            | `everblu/cyble/uptime`          | Device uptime in ISO 8601 format.                                              |

---

## Configuration

### Continuous Integration

This project uses GitHub Actions for automated building, testing, and code quality checks. Every push and pull request triggers builds and quality checks to ensure code quality and compatibility.

The CI workflows include:
- **Build Workflows**: Builds the project for both ESP8266 (huzzah) and ESP32 (esp32dev) platforms to validate that the code compiles successfully
- **Code Quality**: Runs static analysis using cppcheck and formatting checks with clang-format to identify potential issues and maintain code consistency
- **Dependency Caching**: Caches dependencies for faster builds
- **Artifact Upload**: Uploads firmware artifacts and quality reports for successful builds

You can view the build and quality status at the top of this README or in the [Actions tab](https://github.com/genestealer/everblu-meters-esp8266-improved/actions).

### Local Development Setup

1. **Install Required Tools**  
  - Download and install [Visual Studio Code](https://code.visualstudio.com/).  
  - Install the [PlatformIO extension for VS Code](https://platformio.org/). This will install all required dependencies and may require restarting VS Code.

2. **Prepare Configuration Files**  
  - Copy `include/private.example.h` to `include/private.h`.  
  - Update the following details in `private.h`:
    - Wi-Fi and MQTT credentials. If your MQTT setup does not require a username and password, comment out those lines using `//`.  
    - **Meter Serial Number** - Find the serial on your meter label (ignore the manufacturing date):
      - Format: `XX-YYYYYYY-ZZZ` (e.g., "23-1875247-234")
      - Use **first part** for `METER_YEAR` (e.g., 23)
      - Use **middle part** for `METER_SERIAL` (e.g., 1875247)
      - **Ignore the last part** (e.g., ignore -234)
      - **If middle part starts with 0**, omit leading zeros (e.g., "23-0123456-234" → use 123456)
      
      Example:
      ```cpp
      // Serial on meter: 23-1875247-234
      #define METER_YEAR 23       // First part
      #define METER_SERIAL 1875247 // Middle part only
      
      // Serial with leading zeros: 23-0123456-234
      #define METER_YEAR 23       // First part
      #define METER_SERIAL 123456  // Omit leading zeros
      ```
     ![Cyble Meter Label](docs/images/meter_label.jpg)
    - **Wi-Fi PHY Mode**: To enable 802.11g Wi-Fi PHY mode, set `ENABLE_WIFI_PHY_MODE_11G` to `1` in the `private.h` file. By default, it is set to `0` (disabled).
    - Radio debug: control verbose CC1101/RADIAN debug output with `DEBUG_CC1101` in `private.h`.
      - `#define DEBUG_CC1101 1` enables verbose radio debugging (default in the example file).
      - `#define DEBUG_CC1101 0` disables verbose radio debugging.

3. **Update Platform Configuration**  
  - Select an environment in `platformio.ini`:
    - `env:huzzah` (ESP8266 Adafruit HUZZAH)
    - `env:esp32dev` (ESP32 DevKit)
  - Example: build ESP8266 HUZZAH
    - In VS Code, choose the `huzzah` environment and run “Build” or “Upload and Monitor”.
  - Example: build ESP32 DevKit
    - In VS Code, choose the `esp32dev` environment and run “Build” or “Upload and Monitor”.
  - OTA upload is configured only under `env:huzzah` by default. For ESP32, use serial upload unless you add OTA settings for your device’s IP.

4. **Perform Frequency Discovery (First-Time Setup)**  
  - On the very first boot (or anytime there is no stored frequency offset), the firmware automatically launches a wide scan while `AUTO_SCAN_ENABLED` is set to `1` (default).  
  - If you need to skip the scan during development (for example, when you already know the meter's frequency), add `#define AUTO_SCAN_ENABLED 0` to your `include/private.h`.  
  - Compile and upload the code to your ESP device using PlatformIO. Use **PlatformIO > Upload and Monitor**.  
  - **Keep the device connected to your computer during this process.** The serial monitor will display debug output as the device scans frequencies in the 433 MHz range.  
  - **Important**: During the initial scan (first boot with no stored frequency offset), the device performs a wide frequency scan that takes approximately 2 minutes **before** connecting to MQTT. You will see no MQTT/Home Assistant activity during this time - this is normal. Monitor the serial output to see the scan progress. Once the scan completes and the optimal frequency is found, the device will connect to MQTT and publish telemetry data.
  - Once the correct frequency is identified, update the `FREQUENCY` value in `private.h` if needed (the automatic scan stores the offset, so manual adjustment is usually not required).  
  - To re-run the wide scan later, either set `CLEAR_EEPROM_ON_BOOT` to `1` for a single boot cycle or re-enable `AUTO_SCAN_ENABLED`.  
  - For best results, perform this step during local business hours when the meter is most likely to transmit. Refer to the "Frequency Adjustment" section below for additional guidance.

5. **Compile and Flash the Code**  
  - Compile and upload the code to your ESP device using **PlatformIO > Upload and Monitor**.  
  - Keep the device connected to your computer during this process.

6. **Verify Meter Data**  
  - After WiFi and MQTT connection is established (or after the initial frequency scan completes), the meter data should appear in the terminal (bottom panel) and be pushed to MQTT.  
  - If Frequency Discovery is still enabled, its output will also be displayed during this step.
  - **Note**: On first boot with no stored frequency offset, there will be a ~2 minute delay before any MQTT activity while the wide frequency scan runs. This is normal - monitor the serial output to see progress.

7. **Automatic Meter Query**  
  - The device will automatically query the meter once every 24 hours. If the query fails, it will retry every hour until successful.

---

### Reading Schedule

The **Reading Schedule** feature allows you to configure the days when the meter should be queried. By default, the schedule is set to `Monday-Friday`. You can change this in the `private.h` file by modifying the `DEFAULT_READING_SCHEDULE`.

Available options:
- `"Monday-Friday"`: Queries the meter only on weekdays.
- `"Monday-Saturday"`: Queries the meter from Monday to Saturday.
- `"Monday-Sunday"`: Queries the meter every day.

Example configuration in `private.h`:
```cpp
#define DEFAULT_READING_SCHEDULE "Monday-Saturday"
```

### Time zone offset and wake-up window (simple)

Meters often have a local wake window (Time Start/End). The firmware keeps its clock in UTC and applies a simple offset:

Configuration (in `include/private.h`):

```cpp
// Minutes from UTC. Examples: 0 (UTC), 60 (UTC+1), -300 (UTC-5)
#define TIMEZONE_OFFSET_MINUTES 0
```

Behavior:

- The device schedules reads using UTC+offset (your local time). The default read time is 10:00 (local by offset).
- Auto-align can shift the read hour to the meter's wake window (midpoint by default) in local-offset time; the UTC publish is derived from that.

MQTT topics exposed:

- `everblu/cyble/reading_time` – scheduled time in UTC (HH:MM)

In serial logs at startup you’ll see:

- The UTC time pulled from the time server
- The configured offset (minutes)
- The offsetted (UTC+offset) time

---

### Frequency Configuration

The firmware uses **automatic frequency calibration** on the CC1101 radio to ensure accurate communication with your meter. The frequency is configured at compile time in your `private.h` file.

#### How It Works

1. **Compile-Time Configuration**: The operating frequency is set via the `FREQUENCY` define in `private.h` (e.g., `#define FREQUENCY 433.82`).

2. **Default Frequency**: If you don't specify `FREQUENCY` in your config file, the firmware will automatically default to **433.82 MHz**, which is the standard RADIAN protocol center frequency for EverBlu meters. A warning will be logged at startup if the default is used.

3. **Automatic Calibration**: The CC1101 radio performs automatic frequency synthesizer calibration on every idle-to-RX/TX transition (via the `FS_AUTOCAL` setting in `MCSM0`). Additionally, the firmware triggers a manual calibration (`SCAL` strobe) immediately after setting the frequency during initialization.

4. **Frequency Offset Compensation**: The CC1101's built-in frequency offset compensation (FOC) is enabled to automatically correct for small frequency drift during reception.

5. **No MQTT Publishing**: The frequency is **not published to MQTT or Home Assistant**. It's a low-level radio configuration parameter that:
   - Doesn't change during runtime
   - Is already visible in the serial debug log at startup
   - Isn't meaningful as a "sensor" value in Home Assistant
   - Would only add unnecessary clutter to your HA dashboard

#### Why This Approach?

Previous versions required manual frequency scanning and static calibration values. The new approach:
- **Simplifies setup**: Just set your frequency once in `private.h` (or use the default)
- **Improves accuracy**: Automatic calibration adapts to temperature and voltage variations
- **Reduces maintenance**: No need to manually determine and set `FSCAL` values
- **Enhances reliability**: FOC compensates for small frequency errors during reception

#### Configuration Example

In your `private.h`:
```cpp
// Optional: Specify the meter's frequency in MHz
// If not defined, defaults to 433.82 MHz (RADIAN protocol standard)
#define FREQUENCY 433.82
```

To find your meter's exact frequency, you can:
1. Allow the automatic wide scan to run on the first boot (`AUTO_SCAN_ENABLED` defaults to `1`, and clearing EEPROM will force it to run again when needed)
2. Use an RTL-SDR to measure the frequency while a utility reader is polling your meter
3. Start with the default 433.82 MHz and adjust slightly if needed (typically ±0.01 MHz)

The effective frequency is always displayed in the serial log during startup:
```
> Frequency (effective): 433.820000 MHz
```

---

### Adaptive Frequency Management

The firmware includes **automatic frequency adaptation** features to work reliably with any CC1101 module:

1. **Wide Initial Scan (First Boot)**: On first boot (when no frequency offset is stored), the firmware automatically scans ±100 kHz around the base frequency to find your meter. This takes ~1-2 minutes.

2. **Adaptive Frequency Tracking**: After each successful meter read, the firmware monitors the CC1101's FREQEST register and accumulates frequency error. After 10 successful reads, if the average error exceeds 2 kHz, it automatically adjusts the stored frequency offset to compensate for CC1101 crystal drift.

3. **Enhanced FOC**: The CC1101's Frequency Offset Compensation is configured for optimal performance with the EverBlu meter protocol.

#### When to Clear EEPROM

**Always clear EEPROM when you change hardware:**

- **Replace ESP8266/ESP32 board** - Different boards may have slightly different characteristics
- **Replace CC1101 radio module** - Each CC1101 has unique crystal tolerance (typically ±10-50 kHz)
- **Move to a different meter** - Different meters may transmit on slightly different frequencies

**How to clear EEPROM:**

In `include/private.h`, temporarily set:
```cpp
#define CLEAR_EEPROM_ON_BOOT 1
```

Upload firmware and wait for one boot cycle (wide scan will run automatically). Then set back to:
```cpp
#define CLEAR_EEPROM_ON_BOOT 0
```

Upload again to preserve the discovered frequency.

**Why this matters:** The stored frequency offset is specific to your CC1101 module. Using a different CC1101 with the old offset may prevent successful meter communication. Clearing EEPROM forces the firmware to rediscover the optimal frequency for your new hardware.

See `ADAPTIVE_FREQUENCY_FEATURES.md` for detailed technical documentation.

---

## Troubleshooting
### ESP32 build: ModuleNotFoundError: No module named 'intelhex'

This is a PlatformIO tooling dependency used by `esptool.py` to build ESP32 bootloader/partition images. It is not a project file and isn’t committed to the repo. PlatformIO usually manages it automatically, but on some Windows setups it can be missing.

Try the following in order:

- Upgrade PlatformIO core and the Espressif32 platform (from PlatformIO Home → Platforms → Updates), or using the PIO terminal:

  ```powershell
  & "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" upgrade
  & "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" platform update espressif32
  ```

- If it still fails, install the package into PlatformIO’s embedded Python (use the PlatformIO terminal to ensure the right interpreter is used):

  ```powershell
  & "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m pip install --disable-pip-version-check --no-warn-script-location intelhex
  ```

Notes

- Only ESP32 builds use this dependency; ESP8266 builds do not require `intelhex`.
- Prefer the PlatformIO terminal over a global Python to avoid installing into the wrong environment.

### ESP32 compile errors about ESP8266 headers

The code now conditionally includes headers based on the target (ESP8266 vs ESP32). If you still see includes like `ESP8266WiFi.h` during an ESP32 build, ensure you selected the `esp32dev` environment and not an ESP8266 one.


### Frequency Adjustment

Your transceiver module may not be calibrated correctly. Adjust the frequency slightly lower or higher and try again. You may use an RTL-SDR to measure the required offset and rerun the Frequency Discovery code.

### Business Hours

> [!TIP]
> Your meter may be configured to listen for requests only during business hours to conserve energy. If you are unable to communicate with the meter, try again during business hours (8:00–16:00), Monday to Friday. As a rule of thumb, set up your device during business hours to avoid confusion and unnecessary troubleshooting.

> [!NOTE]
> This is particularly relevant in the UK.

### Serial Number Starting with 0

Ignore the leading 0 and provide the serial number in the configuration without it.

### Distance Between Device and Meter

Typically, a CC1101 433 MHz module with an external wire coil antenna has a maximum range of 300–500 m. SMA CC1101 boards with high-gain antennas may increase or even double this range. However, be mindful of the distance for effective use.

---

## ⚠️ Legal Notice

**Radio Licensing**: The 433 MHz ISM band is license-exempt in UK/EU for low-power (<10 mW) use under ETSI EN 300-220.

**Communications Law**: Under UK Wireless Telegraphy Act 2006 Section 48, intercepting radio communications not intended for you may be unlawful, even if it's your own meter. The transmission is technically between the meter and utility.

**Encryption**: Most EverBlu Cyble Enhanced meters transmit unencrypted RADIAN protocol data, making DIY decoding technically feasible.

**Recommendation**: This project is for **personal use on your own property only**. Consider obtaining utility permission. Never use on meters you don't own. Proceed at your own risk.

📄 **For detailed legal analysis, protocol history, and encryption details, see [LEGAL_NOTICE.md](docs/LEGAL_NOTICE.md)**

### Community Resources

- [Maison Simon Wiki (FR) – RADIAN protocol explained](https://lamaisonsimon.fr/wiki/doku.php?id=eau:sonde_eau_radio)
- [ESP8266/ESP32 + CC1101 decoder](https://github.com/neutrinus/everblu-meters)

---
