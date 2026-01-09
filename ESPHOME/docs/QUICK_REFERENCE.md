# ESPHome Quick Reference

Quick reference for EverBlu meter ESPHome configuration.

## Installation

```bash
# Copy component
cp -r ESPHOME/components/everblu_meter /config/esphome/custom_components/

# Use in YAML
external_components:
  - source:
      type: local
      path: custom_components
    components: [ everblu_meter ]
```

## Minimal Configuration

```yaml
time:
  - platform: homeassistant
    id: ha_time

everblu_meter:
  meter_year: 21              # Required: 0-99
  meter_serial: 12345678      # Required: your meter serial
  gdo0_pin: 4                 # Required: CC1101 GDO0 GPIO
  meter_type: water           # Required: water or gas
  time_id: ha_time           # Required: time component
  
  volume:
    name: "Water Volume"      # At least one sensor recommended
```

## Configuration Parameters

| Parameter | Type | Default | Required | Description |
|-----------|------|---------|----------|-------------|
| `meter_year` | int | - | ✅ | Meter year (0-99) |
| `meter_serial` | int | - | ✅ | Serial number |
| `meter_type` | enum | - | ✅ | `water` or `gas` |
| `gdo0_pin` | int | - | ✅ | CC1101 GDO0 GPIO |
| `time_id` | id | - | ✅ | Time component ID |
| `frequency` | float | 433.82 | ❌ | RF frequency (MHz) |
| `auto_scan` | bool | true | ❌ | Auto frequency scan |
| `schedule` | enum | Monday-Friday | ❌ | Reading schedule |
| `read_hour` | int | 10 | ❌ | Read hour (0-23) |
| `read_minute` | int | 0 | ❌ | Read minute (0-59) |
| `max_retries` | int | 10 | ❌ | Max read attempts |
| `retry_cooldown` | duration | 1h | ❌ | Cooldown time |
| `gas_volume_divisor` | int | 100 | ❌ | Gas divisor (100/1000) |

## Schedule Options

- `Monday-Friday` - Weekdays only (default)
- `Saturday` - Saturdays only
- `Sunday` - Sundays only
- `Everyday` - All days

## Available Sensors

### Numeric Sensors

```yaml
everblu_meter:
  volume:                    # Current volume (L or m³)
    name: "Volume"
  battery:                   # Battery life (years)
    name: "Battery"
  counter:                   # Alternative counter
    name: "Counter"
  rssi:                      # Signal strength (dBm)
    name: "RSSI"
  rssi_percentage:           # Signal strength (%)
    name: "Signal"
  lqi:                       # Link quality (0-255)
    name: "LQI"
  lqi_percentage:            # Link quality (%)
    name: "Link Quality"
  time_start:                # Read start time (ms)
    name: "Start Time"
  time_end:                  # Read end time (ms)
    name: "End Time"
  total_attempts:            # Total attempts
    name: "Attempts"
  successful_reads:          # Success count
    name: "Successful"
  failed_reads:              # Failure count
    name: "Failed"
```

### Text Sensors

```yaml
everblu_meter:
  status:                    # Current status
    name: "Status"
  error:                     # Last error
    name: "Error"
  radio_state:               # Radio state
    name: "Radio"
  timestamp:                 # Last reading time
    name: "Timestamp"
```

### Binary Sensors

```yaml
everblu_meter:
  active_reading:            # Reading in progress
    name: "Active"
```

## Common Configurations

### Water Meter - Basic

```yaml
everblu_meter:
  meter_year: 21
  meter_serial: 12345678
  gdo0_pin: 4
  meter_type: water
  time_id: ha_time
  
  volume:
    name: "Water Volume"
    device_class: water
    state_class: total_increasing
  status:
    name: "Status"
```

### Gas Meter - Basic

```yaml
everblu_meter:
  meter_year: 22
  meter_serial: 87654321
  gdo0_pin: 4
  meter_type: gas
  gas_volume_divisor: 100
  time_id: ha_time
  
  volume:
    name: "Gas Volume"
    unit_of_measurement: "m³"
    device_class: gas
    state_class: total_increasing
```

### With Full Monitoring

```yaml
everblu_meter:
  meter_year: 21
  meter_serial: 12345678
  gdo0_pin: 4
  meter_type: water
  time_id: ha_time
  
  volume:
    name: "Volume"
  battery:
    name: "Battery"
  rssi_percentage:
    name: "Signal"
  successful_reads:
    name: "Success Count"
  failed_reads:
    name: "Fail Count"
  status:
    name: "Status"
  error:
    name: "Error"
  active_reading:
    name: "Reading"
```

### Custom Schedule

```yaml
everblu_meter:
  meter_year: 21
  meter_serial: 12345678
  gdo0_pin: 4
  meter_type: water
  time_id: ha_time
  
  # Weekend only, early morning
  schedule: Saturday
  read_hour: 6
  read_minute: 30
  
  # Aggressive retries
  max_retries: 15
  retry_cooldown: 30min
  
  volume:
    name: "Volume"
```

## Wiring Reference

### ESP8266 (D1 Mini)

| CC1101 | D1 Mini | GPIO |
|--------|---------|------|
| VCC    | 3.3V    | -    |
| GND    | GND     | -    |
| SCK    | D5      | 14   |
| MISO   | D6      | 12   |
| MOSI   | D7      | 13   |
| CSN    | D8      | 15   |
| GDO0   | D1      | 5    |
| GDO2   | D2      | 4    |

### ESP32

| CC1101 | ESP32 |
|--------|-------|
| VCC    | 3.3V  |
| GND    | GND   |
| SCK    | 18    |
| MISO   | 19    |
| MOSI   | 23    |
| CSN    | 5     |
| GDO0   | 4     |
| GDO2   | 2     |

⚠️ **Use 3.3V only!** CC1101 is not 5V tolerant.

## Troubleshooting Quick Fixes

### No Readings

```yaml
# Enable auto-scan
everblu_meter:
  auto_scan: true
  max_retries: 15
```

### Poor Signal

```yaml
# Try different frequency
everblu_meter:
  frequency: 433.85  # or 433.80, 433.87
  auto_scan: false
```

### Wrong Volume

```yaml
# Check gas divisor
everblu_meter:
  meter_type: gas
  gas_volume_divisor: 1000  # Try 100 or 1000
```

### Time Issues

```yaml
# Add timezone
everblu_meter:
  timezone_offset: -5  # Hours from UTC
```

## Log Commands

```yaml
logger:
  level: DEBUG
  logs:
    everblu_meter: VERBOSE  # Maximum detail
    sensor: WARN            # Reduce sensor spam
```

## Examples

- [example-water-meter.yaml](example-water-meter.yaml) - Complete water meter
- [example-gas-meter-minimal.yaml](example-gas-meter-minimal.yaml) - Minimal gas meter
- [example-advanced.yaml](example-advanced.yaml) - Advanced features

## Full Documentation

See [ESPHOME_INTEGRATION_GUIDE.md](ESPHOME_INTEGRATION_GUIDE.md) for complete documentation.
