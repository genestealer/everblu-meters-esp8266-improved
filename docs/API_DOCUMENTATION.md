# API Documentation

This document provides comprehensive API documentation for the Everblu Cyble water meter reader project.

## Table of Contents

- [CC1101 Radio Driver API](#cc1101-radio-driver-api)
- [Utility Functions API](#utility-functions-api)
- [State Machine API](#state-machine-api)
- [Frequency Management API](#frequency-management-api)
- [Schedule Management API](#schedule-management-api)
- [Signal Quality API](#signal-quality-api)
- [MQTT Publishing API](#mqtt-publishing-api)

---

## CC1101 Radio Driver API

### Data Structures

#### `struct tmeter_data`

Water meter data structure containing current readings and metadata.

**Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `liters` | `int` | Current water consumption reading in liters |
| `reads_counter` | `int` | Number of times meter has been read (wraps 255→1) |
| `battery_left` | `int` | Estimated battery life remaining in months |
| `time_start` | `int` | Reading window start time (24-hour format, e.g., 8 = 8am) |
| `time_end` | `int` | Reading window end time (24-hour format, e.g., 18 = 6pm) |
| `rssi` | `int` | Radio Signal Strength Indicator (raw value) |
| `rssi_dbm` | `int` | RSSI converted to dBm |
| `lqi` | `int` | Link Quality Indicator (0-255, higher is better) |
| `freqest` | `int8_t` | Frequency offset estimate from CC1101 for adaptive tracking |
| `history[13]` | `uint32_t[]` | Monthly historical readings (13 months), index 0 = oldest, 12 = most recent |
| `history_available` | `bool` | True if historical data was successfully extracted |

### Functions

#### `void setMHZ(float mhz)`

Set the CC1101 radio frequency in MHz.

**Parameters:**
- `mhz`: Frequency in MHz (typically around 433.82 MHz for Cyble meters)

**Purpose:**
Configures the CC1101 transceiver to operate at the specified frequency. Used for fine-tuning frequency to match meter transmissions or for adaptive frequency tracking.

---

#### `bool cc1101_init(float freq)`

Initialize the CC1101 radio transceiver.

**Parameters:**
- `freq`: Initial operating frequency in MHz

**Returns:**
- `true` if initialization succeeded
- `false` on failure

**Purpose:**
Performs complete initialization of the CC1101 radio including:
- SPI communication setup
- Register configuration for RADIAN protocol
- Frequency calibration
- Power amplifier configuration

---

#### `struct tmeter_data get_meter_data(void)`

Read data from Everblu Cyble water meter.

**Returns:**
- `tmeter_data` structure containing all extracted meter data

**Purpose:**
Performs a complete read cycle:
1. Transmits RADIAN protocol request frame to meter
2. Waits for meter response
3. Decodes received data including current reading and history
4. Validates CRC and data integrity
5. Extracts signal quality metrics (RSSI, LQI, frequency offset)

**Note:** This is a blocking operation that may take several seconds to complete.

---

## Utility Functions API

### Debug Output Functions

#### `void show_in_hex(const uint8_t* buffer, size_t len)`

Display buffer contents in hexadecimal format (multi-line).

**Parameters:**
- `buffer`: Pointer to data buffer
- `len`: Length of buffer in bytes

---

#### `void show_in_hex_array(const uint8_t* buffer, size_t len)`

Display buffer contents as hexadecimal array notation.

**Parameters:**
- `buffer`: Pointer to data buffer
- `len`: Length of buffer in bytes

---

#### `void show_in_hex_one_line(const uint8_t* buffer, size_t len)`

Display buffer contents in hexadecimal format (single line).

**Parameters:**
- `buffer`: Pointer to data buffer
- `len`: Length of buffer in bytes

---

#### `void show_in_bin(const uint8_t* buffer, size_t len)`

Display buffer contents in binary format.

**Parameters:**
- `buffer`: Pointer to data buffer
- `len`: Length of buffer in bytes

---

#### `void echo_debug(T_BOOL l_flag, const char *fmt, ...)`

Conditional debug output with printf-style formatting.

**Parameters:**
- `l_flag`: Boolean flag controlling whether to print (true = print)
- `fmt`: Format string (printf-style)
- `...`: Variable arguments for format string

---

### RADIAN Protocol Functions

#### `uint16_t crc_kermit(const unsigned char *input_ptr, size_t num_bytes)`

Calculate Kermit CRC-16 checksum.

**Parameters:**
- `input_ptr`: Pointer to input data buffer
- `num_bytes`: Number of bytes to include in CRC calculation

**Returns:**
- 16-bit CRC checksum value

**Purpose:**
Calculates CRC-16/KERMIT checksum used in RADIAN protocol for data integrity verification. Must call `init_crc_tab()` before first use.

---

#### `int encode2serial_1_3(uint8_t *inputBuffer, int inputBufferLen, uint8_t *outputBuffer)`

Encode buffer using RADIAN 1:3 encoding scheme.

**Parameters:**
- `inputBuffer`: Pointer to input data buffer
- `inputBufferLen`: Length of input buffer in bytes
- `outputBuffer`: Pointer to output buffer (must be >= inputBufferLen * 3)

**Returns:**
- Length of encoded output in bytes

**Purpose:**
Encodes input buffer using proprietary 1:3 encoding required by RADIAN protocol. Output buffer must be at least 3x input buffer size.

---

#### `int Make_Radian_Master_req(uint8_t *outputBuffer, uint8_t year, uint32_t serial)`

Create RADIAN protocol master request frame.

**Parameters:**
- `outputBuffer`: Pointer to output buffer for request frame (must be sufficiently large)
- `year`: Last 2 digits of meter manufacturing year (e.g., 15 for 2015)
- `serial`: Meter serial number (32-bit value)

**Returns:**
- Length of generated request frame in bytes

**Purpose:**
Constructs a complete RADIAN protocol request frame to query a water meter. Frame includes meter identification (year + serial) and proper encoding/CRC.

---

## State Machine API

### Enumerations

#### `enum SystemState`

State machine states for meter reading operation.

| State | Description |
|-------|-------------|
| `STATE_INIT` | Initial state after boot, transitions to IDLE |
| `STATE_IDLE` | Waiting state, checks schedule every 500ms |
| `STATE_CHECK_SCHEDULE` | Evaluates if it's time to read meter |
| `STATE_COOLDOWN_WAIT` | Waiting during retry cooldown period (1 hour) |
| `STATE_START_READING` | Initiates meter read operation |
| `STATE_READING_IN_PROGRESS` | Performing actual meter read (blocking) |
| `STATE_RETRY_WAIT` | Waiting before retry attempt |
| `STATE_PUBLISH_SUCCESS` | Publishing successful read to MQTT |
| `STATE_PUBLISH_FAILURE` | Handling failed read (retry or cooldown) |

### Functions

#### `void enterState(SystemState newState)`

Transition to new state.

**Parameters:**
- `newState`: State to transition to

**Purpose:**
Updates current state, records entry timestamp, and logs transition for debugging purposes.

---

#### `bool isScheduledReadingTime()`

Check if current time matches scheduled reading time.

**Returns:**
- `true` if current time is exactly the scheduled reading time

**Purpose:**
Evaluates whether current time (UTC) matches the configured reading schedule (day of week and exact hour:minute:00).

---

#### `bool isInCooldownPeriod()`

Check if system is in cooldown period.

**Returns:**
- `true` if currently in cooldown period

**Purpose:**
After max retries are exhausted, system enters 1-hour cooldown period to avoid hammering the meter. This function checks if cooldown is active.

---

#### `void handleStateMachine()`

Execute state machine logic.

**Purpose:**
Main state machine handler called from `loop()`. Implements non-blocking meter reading with:
- Schedule checking every 500ms
- Retry logic with exponential backoff
- 1-hour cooldown after max retries
- MQTT status publishing
- Adaptive frequency tracking

**Note:** Must be called frequently from `loop()` for proper operation.

---

## Frequency Management API

### Functions

#### `void saveFrequencyOffset(float offset)`

Save frequency offset to persistent storage.

**Parameters:**
- `offset`: Frequency offset in MHz to be added to base frequency

**Purpose:**
Stores the frequency offset value to EEPROM (ESP8266) or Preferences (ESP32) with a magic number for validation. This offset is applied on next boot.

---

#### `float loadFrequencyOffset()`

Load frequency offset from persistent storage.

**Returns:**
- Frequency offset in MHz, or 0.0 if no valid data found

**Purpose:**
Retrieves previously saved frequency offset from EEPROM (ESP8266) or Preferences (ESP32). Validates data integrity using magic number.

---

#### `void performFrequencyScan()`

Perform narrow-range frequency scan.

**Purpose:**
Scans a narrow frequency range (±0.003 MHz around current frequency) with fine step resolution (0.0005 MHz) to find optimal meter frequency. Used for fine-tuning when close to correct frequency. Updates global frequency offset if better frequency is found.

---

#### `void performWideInitialScan()`

Perform wide-range initial frequency scan.

**Purpose:**
Performs comprehensive frequency scan over wider range (±0.030 MHz) with coarser step resolution (0.001 MHz). Used on first boot or when no stored offset exists. More time-consuming but covers larger uncertainty. Saves discovered offset to persistent storage on success.

---

#### `void adaptiveFrequencyTracking(int8_t freqest)`

Adaptive frequency tracking using FREQEST.

**Parameters:**
- `freqest`: Frequency offset estimate from CC1101 FREQEST register (-128 to +127)

**Purpose:**
Accumulates frequency offset estimates from successful reads and gradually adjusts radio frequency to track meter drift. Uses statistical averaging (ADAPT_THRESHOLD reads) to avoid over-correction on noise.

**Algorithm:**
1. Accumulate FREQEST over multiple reads
2. After threshold reads, calculate average error
3. If average > 2 kHz, apply 50% correction to avoid oscillation
4. Save new offset to persistent storage
5. Reinitialize CC1101 with corrected frequency

**Note:** FREQEST resolution is ~1.59 kHz per LSB (26 MHz crystal).

---

## Schedule Management API

### Functions

#### `bool isValidReadingSchedule(const char* s)`

Validate reading schedule string.

**Parameters:**
- `s`: Schedule string to validate

**Returns:**
- `true` if schedule is valid

**Valid Values:**
- `"Monday-Friday"`
- `"Monday-Saturday"`
- `"Monday-Sunday"`

---

#### `void validateReadingSchedule()`

Validate and correct reading schedule.

**Purpose:**
Checks if current reading schedule is valid. If invalid, falls back to safe default ("Monday-Friday") and logs a warning.

---

#### `bool isReadingDay(struct tm *ptm)`

Check if today is a scheduled reading day.

**Parameters:**
- `ptm`: Pointer to tm structure with current date/time

**Returns:**
- `true` if today is a reading day according to schedule

**Purpose:**
Evaluates whether the current day (based on tm structure) falls within the configured reading schedule.

---

## Signal Quality API

### Functions

#### `int calculateWiFiSignalStrengthPercentage(int rssi)`

Convert WiFi RSSI to percentage.

**Parameters:**
- `rssi`: WiFi RSSI in dBm (typically -100 to -50)

**Returns:**
- Signal strength as percentage (0-100)

**Purpose:**
Maps WiFi RSSI value to 0-100% scale for user-friendly display. Clamps input to reasonable range (-100 to -50 dBm).

---

#### `int calculateMeterdBmToPercentage(int rssi_dbm)`

Convert 433 MHz meter RSSI to percentage.

**Parameters:**
- `rssi_dbm`: Meter RSSI in dBm (typically -120 to -40)

**Returns:**
- Signal strength as percentage (0-100)

**Purpose:**
Converts CC1101 RSSI measurement (in dBm) to 0-100% scale. Uses wider range (-120 to -40 dBm) appropriate for sub-GHz band.

---

#### `int calculateLQIToPercentage(int lqi)`

Convert LQI to percentage.

**Parameters:**
- `lqi`: Link Quality Indicator (0-255, higher is better)

**Returns:**
- Link quality as percentage (0-100)

**Purpose:**
Converts CC1101 Link Quality Indicator (0-255) to 0-100% scale. LQI represents overall link quality including interference effects.

---

## MQTT Publishing API

### Functions

#### `void publishWifiDetails()`

Publish WiFi diagnostics to MQTT.

**Purpose:**
Publishes comprehensive WiFi connection details including:
- SSID and BSSID
- RSSI (raw and percentage)
- IP address and MAC address
- System uptime
- Connection status

**Note:** Called periodically from `loop()` (every 5 minutes).

---

#### `void publishMeterSettings()`

Publish meter configuration to MQTT.

**Purpose:**
Publishes static meter configuration including:
- Meter year and serial number
- Reading schedule (days of week)
- Reading time (UTC, HH:MM format)

**Note:** Called once during connection establishment.

---

#### `void onConnectionEstablished()`

MQTT connection established callback.

**Purpose:**
Called when MQTT connection is successfully established. Performs:
1. NTP time synchronization
2. Arduino OTA initialization
3. Home Assistant MQTT Discovery publishing
4. MQTT topic subscriptions (trigger, frequency_scan)
5. Initial WiFi and meter settings publishing

Also sets up MQTT callbacks for manual trigger and frequency scan commands.

---

## Configuration Validation

### Functions

#### `bool validateConfiguration()`

Validate configuration parameters.

**Returns:**
- `true` if all validations passed
- `false` if any parameter is invalid

**Purpose:**
Performs startup validation of configuration parameters to fail fast on invalid settings.

**Checks:**
- METER_YEAR (must be 0-99)
- METER_SERIAL (must be non-zero)
- FREQUENCY (must be 300-500 MHz if defined)
- READING_HOUR/MINUTE (must be valid time)
- Reading schedule string format

Logs validation results to serial console with ✓ or ERROR markers.

---

## Architecture Overview

### Non-Blocking Operation

The firmware uses a state machine architecture to ensure non-blocking operation:

1. **State Machine**: 9-state FSM manages meter reading lifecycle
2. **Schedule Checking**: Every 500ms check for scheduled time
3. **Retry Logic**: Exponential backoff with max 3 retries
4. **Cooldown Period**: 1-hour cooldown after failed attempts
5. **MQTT Integration**: Publishes status and diagnostics throughout operation

### Frequency Management

Three-tier frequency management system:

1. **Wide Initial Scan**: ±100 kHz scan on first boot (if no saved offset)
2. **Narrow Refinement**: ±15 kHz fine scan around discovered frequency
3. **Adaptive Tracking**: Uses FREQEST to track long-term drift (±2-3 kHz)

### RADIAN Protocol

Proprietary protocol for Everblu Cyble meters:

1. **Frame Construction**: Year + Serial + CRC-16/KERMIT
2. **1:3 Encoding**: Proprietary encoding scheme
3. **Response Decoding**: Extract meter data + 13 months history
4. **Signal Quality**: Extract RSSI, LQI, FREQEST from CC1101

---

## Usage Examples

### Reading Meter Data

```cpp
// Initialize radio at configured frequency
if (!cc1101_init(FREQUENCY)) {
  Serial.println("ERROR: CC1101 init failed");
  return;
}

// Read meter data (blocking operation)
struct tmeter_data data = get_meter_data();

// Check if read was successful
if (data.reads_counter > 0 && data.liters > 0) {
  Serial.printf("Current reading: %d liters\n", data.liters);
  Serial.printf("Battery: %d months\n", data.battery_left);
  Serial.printf("RSSI: %d dBm\n", data.rssi_dbm);
  
  // Access historical data if available
  if (data.history_available) {
    for (int i = 0; i < 13; i++) {
      Serial.printf("Month %d: %u liters\n", i, data.history[i]);
    }
  }
}
```

### Frequency Management

```cpp
// Load stored offset on startup
float offset = loadFrequencyOffset();
Serial.printf("Loaded offset: %.6f MHz\n", offset);

// Initialize with offset
cc1101_init(FREQUENCY + offset);

// After successful read, apply adaptive tracking
adaptiveFrequencyTracking(data.freqest);

// Manually trigger frequency scan
performFrequencyScan();

// Or perform wide initial scan (first boot)
performWideInitialScan();
```

### State Machine Integration

```cpp
void loop() {
  mqtt.loop();           // Handle MQTT
  ArduinoOTA.handle();   // Handle OTA updates
  handleStateMachine();  // Execute state machine
  
  // Periodic WiFi diagnostics
  if (millis() - lastWifiUpdate > 300000) {
    publishWifiDetails();
    lastWifiUpdate = millis();
  }
}
```

---

## Dependencies

- **ESP8266/ESP32**: Arduino core
- **EspMQTTClient**: MQTT client library
- **ArduinoOTA**: Over-the-air updates
- **CC1101**: SPI-based radio transceiver
- **Time**: NTP time synchronization

---

## References

- [CC1101 Datasheet](https://www.ti.com/product/CC1101)
- [RADIAN Protocol Implementation Notes](../README.md)
- [Frequency Calibration Guide](../FREQUENCY_CALIBRATION_CHANGES.md)
- [Adaptive Frequency Features](../ADAPTIVE_FREQUENCY_FEATURES.md)
