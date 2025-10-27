# Copilot instructions for everblu-meters-esp8266-improved

Purpose: Firmware for ESP8266/ESP32 + CC1101 to actively poll Itron EverBlu Cyble Enhanced (RADIAN at 433 MHz), then publish readings to Home Assistant via MQTT AutoDiscovery.

## Big picture
- Entry point: `src/main.cpp` orchestrates Wi‑Fi/MQTT, CC1101 radio, and HA discovery.
- Radio layer: `src/cc1101.cpp` and `include/cc1101.h` implement CC1101 SPI control, RADIAN wake/poll, bitstream decoding, and parsing to `tmeter_data`.
- Protocol helpers: `include/everblu_meters.h` and `src/utils.cpp` provide frame encoding, CRC (Kermit), and small debug utilities.
- Configuration: `include/config.h` (must exist in include path) defines Wi‑Fi/MQTT secrets, meter serial/year, GDO0 pin, and radio frequency.
- Data flow: at boot → Wi‑Fi+MQTT → HA discovery → scheduled or manual "read" → `get_meter_data()` → publish metrics to MQTT topics under `everblu/cyble/*`.

## Build/flash workflow (PlatformIO)
- Platform selection lives in `platformio.ini`. One env is active (default `env:huzzah` ESP8266 + Arduino). Change env to target another board.
- Serial monitor at 115200 baud; upload at 460800 baud by default.
- OTA is supported (ArduinoOTA). To use it, uncomment `upload_protocol = espota` and `upload_port = <device-ip>` in `platformio.ini`.

## Required configuration (do this first)
- Copy `include/config.example.h` to `include/config.h` (exact lowercase name to match `#include "config.h"`). Fill values:
  - `secret_wifi_ssid`, `secret_wifi_password`, `secret_mqtt_server`, optional `secret_mqtt_username/password`, `secret_clientName`, `secret_local_timeclock_server`.
  - Meter info: `METER_YEAR` (YY), `METER_SERIAL` (no leading 0), `FREQUENCY` (MHz float), `GDO0` (default 5 on ESP8266).
- Keep secrets out of commits. Use the example file for diffs and don't check real creds into VCS.

## Radio/protocol essentials
- Frequency: Configure the target frequency in `FREQUENCY` in `config.h` (typically ~433.82 MHz, but varies by meter/region).
  - The CC1101 now uses **automatic frequency synthesizer calibration** and **frequency offset compensation (FOC)** for improved accuracy and reliability.
  - Manual calibration is performed at init via the `SCAL` strobe command.
  - Automatic calibration occurs on every IDLE→RX/TX transition (configured in MCSM0 register).
  - FOC automatically compensates for frequency drift during reception (configured in FOCCFG register).
- Reading pipeline: `get_meter_data()` sends a wake preamble, RADIAN poll (`Make_Radian_Master_req`), then receives/decodes; returns `tmeter_data { liters, reads_counter, battery_left, time_start, time_end, rssi, rssi_dbm, lqi }`.

## MQTT and Home Assistant
- Sensor topics (retained):
  - `everblu/cyble/liters`, `battery`, `counter`, `rssi_dbm`, `rssi_percentage`, `lqi`, `lqi_percentage`, `time_start`, `time_end`, `timestamp`.
  - Wi‑Fi/diag: `wifi_ip`, `wifi_rssi`, `wifi_signal_percentage`, `mac_address`, `ssid`, `bssid`, `uptime`, `status` (LWT online/offline), `active_reading`.
- Commands:
  - `everblu/cyble/trigger` → any payload triggers immediate read.
  - `everblu/cyble/restart` → payload `restart` reboots device.
- HA AutoDiscovery: Sent on connect in `onConnectionEstablished()` using compact JSON strings. Discovery topics are under `homeassistant/<component>/<object_id>/config` and are retained. If you add a new metric, add both a discovery JSON and a publish in the corresponding data/diag function.

## Scheduling and timing
- Daily reading at 10:00 UTC when `isReadingDay()` matches. Default schedule is `Monday‑Friday` via `DEFAULT_READING_SCHEDULE` (override in `config.h`).
- Wi‑Fi diagnostics publish every 5 minutes.

## Hardware/pins
- Uses hardware SPI: on ESP8266 SCK=GPIO14, MISO=GPIO12, MOSI=GPIO13, CS=GPIO15; GDO0 default GPIO5 (configurable via `GDO0`). LED_BUILTIN indicates activity.
- CC1101 init failure causes an error blink loop; check wiring and power (3.3V only).

## Project conventions and extension points
- MQTT root is fixed to `everblu/cyble/`. Keep naming consistent; add new topics under this root.
- All MQTT publishes are retained; delays between publishes are small `delay(50)` bursts—maintain similar pacing for stability.
- Add new diagnostics in:
  - `publishWifiDetails()` (Wi‑Fi/uptime),
  - `publishMeterSettings()` (static config),
  - `onUpdateData()` (per‑read meter values),
  - plus a matching discovery JSON near existing ones.
- If supporting a new board, add/select an env in `platformio.ini`, verify SPI pins, and adjust `GDO0` and `LED_BUILTIN` as needed.

## Quick debug tips
- Use serial monitor at 115200. Enable `mqtt.enableDebuggingMessages(true)` if needed.
- If no data, try during business hours (meters wake intermittently), verify frequency is correct in `config.h`, and check antenna/CC1101 variant.
- The CC1101 automatically calibrates on every state transition and compensates for frequency offset, improving reliability over a wide temperature range.

If any part of this is unclear (e.g., secret file naming/casing, adding new HA sensors, or adapting to ESP32), tell me what you’d like to do and I’ll refine these instructions. 