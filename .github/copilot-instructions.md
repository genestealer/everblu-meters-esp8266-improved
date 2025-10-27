# Copilot instructions for everblu-meters-esp8266-improved

Purpose: Firmware for ESP8266/ESP32 + CC1101 to actively poll Itron EverBlu Cyble Enhanced (RADIAN at 433 MHz), then publish readings to Home Assistant via MQTT AutoDiscovery.

## Big picture
- Entry point: `src/everblu-meters-esp8266.cpp` orchestrates Wi‑Fi/MQTT, CC1101 radio, and HA discovery.
- Radio layer: `src/cc1101.{h,cpp}` implements CC1101 SPI control, RADIAN wake/poll, bitstream decoding, and parsing to `tmeter_data`.
- Protocol helpers: `src/everblu_meters.{h,cpp}` provide frame encoding, CRC (Kermit), and small debug utilities.
- Configuration: `private.h` (must exist in include path) defines Wi‑Fi/MQTT secrets, meter serial/year, GDO0 pin, and radio frequency.
- Data flow: at boot → Wi‑Fi+MQTT → HA discovery → scheduled or manual "read" → `get_meter_data()` → publish metrics to MQTT topics under `everblu/cyble/*`.

## Build/flash workflow (PlatformIO)
- Platform selection lives in `platformio.ini`. One env is active (default `env:huzzah` ESP8266 + Arduino). Change env to target another board.
- Serial monitor at 115200 baud; upload at 460800 baud by default.
- OTA is supported (ArduinoOTA). To use it, uncomment `upload_protocol = espota` and `upload_port = <device-ip>` in `platformio.ini`.

## Required configuration (do this first)
- Copy `include/Example_Private.h` to `include/private.h` (exact lowercase name to match `#include "private.h"`). Fill values:
  - `secret_wifi_ssid`, `secret_wifi_password`, `secret_mqtt_server`, optional `secret_mqtt_username/password`, `secret_clientName`, `secret_local_timeclock_server`.
  - Meter info: `METER_YEAR` (YY), `METER_SERIAL` (no leading 0), `FREQUENCY` (MHz float), `GDO0` (default 5 on ESP8266).
- Keep secrets out of commits. Use the example file for diffs and don’t check real creds into VCS.

## Radio/protocol essentials
- Frequency: start with ~433.82 MHz. You can:
  - Compile-time scan: set `SCAN_FREQUENCY_433MHZ 1` in `private.h` to sweep 433.76–433.89 MHz in 0.5 kHz steps (blocks until hit).
  - Runtime scan: via MQTT, publish `start`/`stop` to `everblu/cyble/scan`. Scan runs non‑blocking in two phases (25 kHz coarse → 1 kHz fine). Result at `everblu/cyble/discovered_frequency`.
- Reading pipeline: `get_meter_data()` sends a wake preamble, RADIAN poll (`Make_Radian_Master_req`), then receives/decodes; returns `tmeter_data { liters, reads_counter, battery_left, time_start, time_end, rssi, rssi_dbm, lqi }`.

## MQTT and Home Assistant
- Sensor topics (retained):
  - `everblu/cyble/liters`, `battery`, `counter`, `rssi_dbm`, `rssi_percentage`, `lqi`, `lqi_percentage`, `time_start`, `time_end`, `timestamp`.
  - Wi‑Fi/diag: `wifi_ip`, `wifi_rssi`, `wifi_signal_percentage`, `mac_address`, `ssid`, `bssid`, `uptime`, `status` (LWT online/offline), `active_reading`, scan topics above.
- Commands:
  - `everblu/cyble/trigger` → any payload triggers immediate read.
  - `everblu/cyble/restart` → payload `restart` reboots device.
  - `everblu/cyble/scan` → `start`/`stop` controls runtime scan.
- HA AutoDiscovery: Sent on connect in `onConnectionEstablished()` using compact JSON strings. Discovery topics are under `homeassistant/<component>/<object_id>/config` and are retained. If you add a new metric, add both a discovery JSON and a publish in the corresponding data/diag function.

## Scheduling and timing
- Daily reading at 10:00 UTC when `isReadingDay()` matches. Default schedule is `Monday‑Friday` via `DEFAULT_READING_SCHEDULE` (override in `private.h`).
- While a runtime frequency scan is active, scheduled reads are deferred.
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
- If no data, try during business hours (meters wake intermittently), verify frequency via scan, and check antenna/CC1101 variant.

If any part of this is unclear (e.g., secret file naming/casing, adding new HA sensors, or adapting to ESP32), tell me what you’d like to do and I’ll refine these instructions. 