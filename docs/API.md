# Project API and Integration Guide

This document describes the public-facing interfaces of the EverBlu ESP8266/ESP32 firmware: configuration knobs, C APIs, MQTT topics/commands, and the runtime state machine and frequency management.

## Overview

- Targets: ESP8266 (HUZZAH), ESP32 DevKit
- Radio: CC1101 (433 MHz) for Itron EverBlu (RADIAN protocol)
- Transport: MQTT with Home Assistant AutoDiscovery
- Scheduling: Daily read window with non-blocking state machine
- Frequency handling: Initial wide scan, on-demand scan, and adaptive tracking using CC1101 FREQEST

---

## Configuration (include/config.h)

These macros control build- and runtime behavior. Omit any optional macro to use sensible defaults; the firmware validates important parameters at boot and logs warnings or halts on fatal errors.

- Wi-Fi and MQTT
  - `#define secret_wifi_ssid "..."`
  - `#define secret_wifi_password "..."`
  - `#define secret_mqtt_server "192.168.x.x"`
  - `#define secret_clientName "EverblueCyble"`
  - `#define secret_mqtt_username "..."` (optional)
  - `#define secret_mqtt_password "..."` (optional)
  - `#define secret_local_timeclock_server "..."` (NTP/chrony server)
- Radio and meter identity
  - `#define METER_YEAR 20` (00–99)
  - `#define METER_SERIAL 257750`
  - `#define GDO0 5` (CC1101 GDO0 pin)
  - `#define FREQUENCY 433.82` (MHz; default if omitted)
- Reading schedule and timing (optional)
  - `#define DEFAULT_READING_SCHEDULE "Monday-Friday"` (also: "Monday-Saturday", "Monday-Sunday")
  - `#define DEFAULT_READING_HOUR_UTC 10`
  - `#define DEFAULT_READING_MINUTE_UTC 0`
  - `#define AUTO_ALIGN_READING_TIME 1` (1=enable alignment to meter wake window)
  - `#define AUTO_ALIGN_USE_MIDPOINT 1` (0=time_start, 1=midpoint)
- Housekeeping
  - `#define ENABLE_WIFI_PHY_MODE_11G 0` (ESP8266 only)
  - `#define CLEAR_EEPROM_ON_BOOT 0` (set to 1 once to force frequency re-discovery, then back to 0)

Validation rules at boot:

- METER_YEAR must be 0–99; METER_SERIAL must be non-zero.
- FREQUENCY must be within 300–500 MHz if defined; otherwise 433.820000 MHz default is used.
- Reading schedule must be one of the three supported strings; invalid values fall back to Monday–Friday with a warning.


---

## C APIs (src/*.h)

Available to other translation units within the firmware.

- CC1101 (src/cc1101.h)
  - `void setMHZ(float mhz);`
  - `bool cc1101_init(float freq);` Initialize the radio synthesizer and configuration at `freq` MHz. Returns true on success.
  - `tmeter_data get_meter_data(void);` Perform a RADIAN read; returns parsed data. Structure fields:
    - `int liters`
    - `int reads_counter` (wraps 255→1)
    - `int battery_left` (months)
    - `int time_start` (0–23)
    - `int time_end` (0–23)
    - `int rssi` (raw)
    - `int rssi_dbm` (dBm)
    - `int lqi` (0–255)
    - `int8_t freqest` (CC1101 frequency error estimate)
- Utils (src/utils.h)
  - `uint16_t crc_kermit(const unsigned char *input_ptr, size_t num_bytes);`
  - Debug helpers (serial hex/bin dumps): `show_in_hex*`, `show_in_bin`
  - RADIAN helpers: `int Make_Radian_Master_req(...)`, `int encode2serial_1_3(...)`

Notes

- `get_meter_data` performs a single read attempt and returns best-effort data; orchestrated retries and scheduling are handled by the app state machine in `main.cpp`.

---

## MQTT Interface

All telemetry is published under the `everblu/cyble/` prefix. Home Assistant discovery topics are published under `homeassistant/.../config` at connect time.

### Telemetry (publish)

- Meter data
  - `everblu/cyble/liters` (int)
  - `everblu/cyble/counter` (int)
  - `everblu/cyble/battery` (months)
  - `everblu/cyble/rssi_dbm` (dBm)
  - `everblu/cyble/rssi_percentage` (0–100)
  - `everblu/cyble/lqi` (0–255)
  - `everblu/cyble/lqi_percentage` (0–100)
  - `everblu/cyble/time_start` (HH:MM)
  - `everblu/cyble/time_end` (HH:MM)
  - `everblu/cyble/timestamp` (ISO-8601, UTC)
- Device and diagnostics
  - `everblu/cyble/status` (online|offline) [LWT]
  - `everblu/cyble/active_reading` (true|false)
  - `everblu/cyble/cc1101_state` (Idle|Reading|Frequency Scanning|Initial Frequency Scan)
  - `everblu/cyble/status_message` (free-form text)
  - `everblu/cyble/total_attempts`, `successful_reads`, `failed_reads` (counters)
  - `everblu/cyble/last_error` (string)
  - `everblu/cyble/frequency_offset` (kHz; stored offset, formatted with 3 decimals)
- Wi‑Fi
  - `everblu/cyble/wifi_ip`
  - `everblu/cyble/wifi_rssi` (dBm)
  - `everblu/cyble/wifi_signal_percentage` (0–100)
  - `everblu/cyble/ssid`, `bssid`, `mac_address`
  - `everblu/cyble/uptime` (timestamp when device booted)
- Config snapshot
  - `everblu/cyble/water_meter_year` (00–99)
  - `everblu/cyble/water_meter_serial`
  - `everblu/cyble/reading_schedule` (text)
  - `everblu/cyble/reading_time` (HH:MM, UTC; may be auto-aligned to meter wake window)

All published topics are retained (retain=true) to simplify HA and diagnostics.

### Commands (subscribe)

- `everblu/cyble/trigger` → request a read
  - Payload: `update` or `read`
  - Cooldown: Ignored if a failure cooldown is active (1 hour after max retries)
- `everblu/cyble/restart` → reboot device
  - Payload: `restart`
- `everblu/cyble/frequency_scan` → run a ±30 kHz scan around the base frequency
  - Payload: `scan`

Invalid payloads are rejected with a warning to serial and a `status_message` publish.

### Home Assistant Discovery

On MQTT connect, the firmware publishes discovery JSON for all sensors, binary sensors, and buttons (restart, request read, frequency scan). Units and classes are set to align with HA best practices (e.g., frequency offset in kHz, RSSI class for signal, total_increasing for counters).

---

## Runtime State Machine (src/main.cpp)

The main loop is non-blocking and orchestrates meter reads safely.

States

- `APP_WAIT_SCHEDULE` (initial) → waits for the daily schedule tick
- `APP_READ_REQUESTED` → a read has been requested (schedule or MQTT)
- `APP_READING` → active read via `onUpdateData()`
- `APP_RETRY_WAIT` → waiting until `appNextActionAt` for retry (set by `onUpdateData` on transient failure)
- `APP_IDLE` → placeholder (behaves like wait)

Transitions

- Schedule tick at configured time → `APP_READ_REQUESTED`
- MQTT trigger → `APP_READ_REQUESTED` (if not cooling down)
- After `onUpdateData` returns → `APP_WAIT_SCHEDULE`
- On retry request → `APP_RETRY_WAIT` until `millis() >= appNextActionAt`

Safety

- Watchdog is fed during scans and longer loops
- Recursion replaced with timed retries; no nested delayed callbacks
- Cooldown enforced after exceeding `MAX_RETRIES` (1 hour)

---

## Frequency Management

- Persistent offset storage
  - ESP8266: EEPROM (addressed with a magic value)
  - ESP32: Preferences (namespace `everblu`, key `freq_offset`)
- Boot behavior
  - If no offset found and auto-scan enabled, a wide initial scan runs (±100 kHz, coarse step), followed by a fine scan; the best offset is saved and applied
- On-demand scan
  - `performFrequencyScan()`: ±30 kHz in 5 kHz steps; saves best offset and republishes `frequency_offset` (kHz)
- Adaptive tracking
  - `adaptiveFrequencyTracking(int8_t freqest)`: accumulates FREQEST and applies a gentle correction after `ADAPT_THRESHOLD` successful reads when the average error exceeds ~2 kHz; republishes `frequency_offset`

---

## Error Handling and Retries

- Per-read retries up to `MAX_RETRIES` (3)
- On failure beyond retries: device publishes metrics and enters a 1-hour cooldown (`RETRY_COOLDOWN`) before accepting new triggers
- `last_error` is updated with the latest error context

---

## Testing

- Unity-based PlatformIO tests live under `test/`
- Example: CRC Kermit tests validate `utils.cpp` implementation
- Run with `pio test` (from the PlatformIO terminal so the bundled Python is used)

---

## Notes

- Counter semantics: `reads_counter` wraps 255→1 (documented in code and README)
- Frequency offset publishes are in kHz (discovery unit and values)
- On first boot, MQTT discovery is published after initial scan/time setup; reads will only start once scans and scheduling are ready
