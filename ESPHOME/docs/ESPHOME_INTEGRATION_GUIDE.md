# ESPHome Integration Guide

This guide explains how to use the EverBlu Cyble Enhanced meter reader with ESPHome, allowing seamless integration with Home Assistant.

## Table of Contents

- [Overview](#overview)
- [Requirements](#requirements)
- [Installation](#installation)
- [Configuration](#configuration)
- [Sensors](#sensors)
- [Examples](#examples)
- [Troubleshooting](#troubleshooting)

## Overview

The ESPHome integration allows you to read EverBlu Cyble Enhanced water and gas meters directly within the ESPHome ecosystem. Key features include:

- **Native ESPHome Component**: Integrates seamlessly with ESPHome's sensor framework
- **Automatic Discovery**: Sensors appear automatically in Home Assistant
- **Scheduled Readings**: Configure when and how often to read the meter
- **Comprehensive Monitoring**: Track signal quality, battery life, and reading statistics
- **Multiple Meter Types**: Supports both water and gas meters

## Requirements

### Hardware

- **ESP8266** or **ESP32** board (ESP8266 D1 Mini recommended)
- **CC1101 RF transceiver** module (868/915 MHz version)
- **EverBlu Cyble Enhanced** meter with RF module installed

### Wiring

Connect the CC1101 to your ESP board:

| CC1101 Pin | ESP8266 (D1 Mini) | ESP32        |
|------------|-------------------|--------------|
| GND        | GND               | GND          |
| VCC        | 3.3V              | 3.3V         |
| SCK        | D5 (GPIO14)       | GPIO18       |
| MISO       | D6 (GPIO12)       | GPIO19       |
| MOSI       | D7 (GPIO13)       | GPIO23       |
| CSN        | D8 (GPIO15)       | GPIO5        |
| GDO0       | D1 (GPIO5)        | GPIO4        |
| GDO2       | D2 (GPIO4)        | GPIO2        |

⚠️ **Important**: The CC1101 requires 3.3V power. Do not connect to 5V!

### Software

- ESPHome 2023.x or later
- Home Assistant (optional, but recommended)

## Installation

### Step 1: Clone the Repository

```bash
git clone https://github.com/yourusername/everblu-meters-esp8266-improved.git
cd everblu-meters-esp8266-improved
```

### Step 2: Use External Components (Recommended)

**Important**: Before using external_components, you must prepare the component by copying source dependencies. Run this from the repository root:

```bash
./prepare-component-release.sh
```

This script copies all required files from `src/` into `ESPHOME/components/everblu_meter/src/`, making the component self-contained.

Then use ESPHome's `external_components` feature in your YAML configuration:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/yourusername/everblu-meters-esp8266-improved
      ref: main
    components: [ everblu_meter ]
    refresh: 1d
```

Alternatively, for local development:

```yaml
external_components:
  - source:
      type: local
      path: /path/to/everblu-meters-esp8266-improved/ESPHOME/components
    components: [ everblu_meter ]
```

### Step 2 Alternative: Manual Installation

If you prefer to copy files locally:

```bash
# From repository root
./prepare-component-release.sh

# Then copy to ESPHome config
cp -r ESPHOME/components/everblu_meter /config/esphome/custom_components/
```

**Note**: The `prepare-component-release.sh` script must be run whenever source files are updated.

### Step 3: Create Your Configuration

Copy one of the example configurations:

```bash
cp ESPHOME/example-water-meter.yaml my-water-meter.yaml
```

Edit the configuration file to match your setup (see Configuration section).

### Step 4: Compile and Upload

```bash
esphome run my-water-meter.yaml
```

## Configuration

### Basic Configuration

The minimal configuration requires:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/yourusername/everblu-meters-esp8266-improved
      ref: main
    components: [ everblu_meter ]
    refresh: 1d

time:
  - platform: homeassistant
    id: ha_time

everblu_meter:
  meter_year: 21              # Last 2 digits of manufacture year
  meter_serial: 12345678      # Your meter's serial number
  meter_type: water           # 'water' or 'gas'
  time_id: ha_time
  
  volume:
    name: "Water Volume"
```

### Required Parameters

| Parameter | Type | Description | Example |
|-----------|------|-------------|---------|
| `meter_year` | int | Last 2 digits of meter manufacture year (00-99) | `21` |
| `meter_serial` | int | Meter serial number | `12345678` |
| `meter_type` | string | Type of meter: `water` or `gas` | `water` |

### Optional Parameters

#### Radio Configuration

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `frequency` | float | `433.82` | RF frequency in MHz (433.0-434.8) |
| `auto_scan` | bool | `true` | Automatically scan for optimal frequency |

#### Schedule Configuration

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `reading_schedule` | string | `Monday-Friday` | When to read: `Monday-Friday`, `Saturday`, `Sunday`, or `Everyday` |
| `read_hour` | int | `10` | Hour to perform reading (0-23) |
| `read_minute` | int | `0` | Minute to perform reading (0-59) |
| `timezone_offset` | int | `0` | Hours offset from UTC (-12 to +14) |

#### Time Alignment

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `auto_align_time` | bool | `true` | Align readings to configured time |
| `auto_align_midpoint` | bool | `true` | Use midpoint of reading window |

#### Retry Configuration

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `max_retries` | int | `10` | Maximum number of read attempts |
| `retry_cooldown` | duration | `1h` | Cooldown between retry sessions |

#### Gas Meter Specific

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `gas_volume_divisor` | int | `100` | Volume divisor for gas meters (100 or 1000) |

### Finding Your Meter Information

#### Meter Year and Serial Number

Look for a label on your meter that shows:
- **Year**: Usually 2 digits (e.g., "21" for 2021)
- **Serial Number**: 8-digit number

Example label: `21-12345678` means year=21, serial=12345678

#### Frequency

The default frequency (433.82 MHz) works for most European meters. If you experience reading issues:

1. Enable `auto_scan: true` (default)
2. Check logs for detected frequency
3. Set `frequency` to the detected value
4. Disable `auto_scan: false` for faster readings

## Sensors

The component provides various sensors for monitoring your meter:

### Numeric Sensors

| Sensor | Unit | Description | State Class |
|--------|------|-------------|-------------|
| `volume` | L or m³ | Current volume reading | `total_increasing` |
| `counter` | L or m³ | Alternative volume counter | `total_increasing` |
| `battery` | years | Estimated battery life remaining | `measurement` |
| `rssi` | dBm | Radio signal strength | `measurement` |
| `rssi_percentage` | % | Signal strength as percentage (0-100) | `measurement` |
| `lqi` | - | Link quality indicator (0-255) | `measurement` |
| `lqi_percentage` | % | Link quality as percentage (0-100) | `measurement` |
| `time_start` | ms | Reading start timestamp | `measurement` |
| `time_end` | ms | Reading end timestamp | `measurement` |
| `total_attempts` | - | Total read attempts | `total_increasing` |
| `successful_reads` | - | Number of successful reads | `total_increasing` |
| `failed_reads` | - | Number of failed reads | `total_increasing` |

### Text Sensors

| Sensor | Description | Values |
|--------|-------------|--------|
| `status` | Current meter status | `Idle`, `Reading`, `Success`, `Error` |
| `error` | Last error message | Error description or empty |
| `radio_state` | Current radio state | `Init`, `Scanning`, `Receiving`, `Idle` |
| `timestamp` | Last successful reading timestamp | ISO 8601 format |

### Binary Sensors

| Sensor | Description | Device Class |
|--------|-------------|--------------|
| `active_reading` | Whether a reading is in progress | `running` |

### Sensor Configuration

Each sensor supports standard ESPHome sensor options:

```yaml
everblu_meter:
  volume:
    name: "Water Volume"
    unit_of_measurement: "L"
    device_class: water
    state_class: total_increasing
    accuracy_decimals: 1
    icon: "mdi:water"
    filters:
      - throttle: 10s
      - delta: 0.1
```

## Examples

### Example 1: Basic Water Meter

Simple configuration with essential sensors:

```yaml
everblu_meter:
  meter_year: 21
  meter_serial: 12345678
  meter_type: water
  time_id: ha_time
  
  volume:
    name: "Water Volume"
    device_class: water
    state_class: total_increasing
  
  status:
    name: "Meter Status"
```

See [example-water-meter.yaml](example-water-meter.yaml) for complete configuration.

### Example 2: Minimal Gas Meter

Minimal configuration for gas meter:

```yaml
everblu_meter:
  meter_year: 22
  meter_serial: 87654321
  meter_type: gas
  gas_volume_divisor: 100
  time_id: ha_time
  
  volume:
    name: "Gas Volume"
    unit_of_measurement: "m³"
    device_class: gas
    state_class: total_increasing
```

See [example-gas-meter-minimal.yaml](example-gas-meter-minimal.yaml) for complete configuration.

### Example 3: Advanced Monitoring

Full monitoring with all sensors and custom scheduling:

```yaml
everblu_meter:
  meter_year: 23
  meter_serial: 12345678
  meter_type: water
  
  # Weekend readings only
  reading_schedule: Saturday
  read_hour: 6
  read_minute: 30
  
  # Fast retry strategy
  max_retries: 15
  retry_cooldown: 30min
  
  # All sensors enabled
  volume:
    name: "Water Volume"
  battery:
    name: "Meter Battery"
  rssi_percentage:
    name: "Signal Quality"
  successful_reads:
    name: "Successful Reads"
  status:
    name: "Status"
  # ... (more sensors)
```

See [example-advanced.yaml](example-advanced.yaml) for complete configuration.

## Troubleshooting

### No Readings Received

**Symptoms**: Meter status stays "Idle" or shows errors

**Solutions**:

1. **Check wiring**: Verify all CC1101 connections
2. **Check frequency**: Enable `auto_scan: true` and monitor logs
3. **Check distance**: Move ESP closer to meter (max ~10m)
4. **Check schedule**: Ensure current day/time matches configuration
5. **Check time sync**: Verify time component is synchronized

### Poor Signal Quality

**Symptoms**: Low RSSI/LQI values, frequent failed reads

**Solutions**:

1. **Reduce distance**: Move ESP closer to meter
2. **Improve antenna**: Use external antenna on CC1101
3. **Reduce interference**: Move away from other RF devices
4. **Adjust frequency**: Try nearby frequencies (±0.05 MHz)

### Inconsistent Readings

**Symptoms**: Volume jumps or doesn't update

**Solutions**:

1. **Check meter info**: Verify year and serial number are correct
2. **Check gas divisor**: Gas meters may use 100 or 1000
3. **Increase retries**: Set `max_retries: 15` or higher
4. **Check logs**: Look for error messages

### High Failed Read Rate

**Symptoms**: `failed_reads` increases faster than `successful_reads`

**Solutions**:

1. **Optimize schedule**: Read during quiet times (low water/gas usage)
2. **Increase cooldown**: Set `retry_cooldown: 2h` or longer
3. **Adjust timing**: Try different `read_hour` values
4. **Check battery**: Low meter battery affects RF transmission

### Time Not Synchronized

**Symptoms**: "Time not synchronized" errors in logs

**Solutions**:

1. **Check time component**: Ensure `time:` platform is configured
2. **Wait for sync**: Allow 1-2 minutes after boot
3. **Check network**: Verify WiFi connection to Home Assistant
4. **Manual timezone**: Set `timezone_offset` if time component unavailable

### Component Not Found

**Symptoms**: "Component everblu_meter not found" error

**Solutions**:

1. **Check path**: Verify `external_components` path is correct
2. **Check files**: Ensure all component files exist
3. **Rebuild**: Try `esphome clean my-config.yaml` then rebuild
4. **Update ESPHome**: Upgrade to latest version

## Advanced Topics

### Custom Reading Logic

You can add automations based on sensor values:

```yaml
everblu_meter:
  status:
    name: "Meter Status"
    on_value:
      then:
        - if:
            condition:
              text_sensor.state:
                id: status
                state: "Error"
            then:
              # Send notification
              - homeassistant.service:
                  service: notify.mobile_app
                  data:
                    message: "Meter reading failed"
```

### Consumption Calculation

Calculate daily consumption using template sensors:

```yaml
sensor:
  - platform: template
    name: "Daily Consumption"
    unit_of_measurement: "L"
    lambda: |-
      static float last = 0;
      float current = id(my_meter).volume_sensor->state;
      float consumption = current - last;
      last = current;
      return consumption;
```

### Multiple Meters

You can configure multiple meters on one ESP:

```yaml
everblu_meter:
  - id: water_meter
    meter_year: 21
    meter_serial: 12345678
    meter_type: water
    volume:
      name: "Water Volume"
  
  - id: gas_meter
    meter_year: 22
    meter_serial: 87654321
    meter_type: gas
    volume:
      name: "Gas Volume"
```

**Note**: Reading multiple meters requires careful scheduling to avoid conflicts.

## Developer Notes

### Architecture Overview

The ESPHome component uses **dependency injection** to achieve maximum code reusability. The core meter reading logic is platform-agnostic and shared between standalone MQTT and ESPHome modes (~95% code sharing).

#### Modular Components

The codebase is organized into three layers:

1. **Core Services** (`src/services/`)
   - `MeterReader`: Platform-agnostic meter reading orchestrator
   - `FrequencyManager`: Frequency calibration and optimization
   - `ScheduleManager`: Daily reading schedule logic
   - `StorageAbstraction`: Platform-independent persistent storage

2. **Adapter Interfaces** (`src/adapters/`)
   - `IConfigProvider`: Abstract configuration interface
   - `ITimeProvider`: Abstract time synchronization interface
   - `IDataPublisher`: Abstract data publishing interface

3. **ESPHome Adapters** (`src/adapters/implementations/`)
   - `ESPHomeConfigProvider`: Reads configuration from YAML
   - `ESPHomeTimeProvider`: Uses ESPHome's RealTimeClock
   - `ESPHomeDataPublisher`: Publishes directly to ESPHome sensors

#### Dependency Injection Pattern

The component wires up adapters in the `setup()` method:

```cpp
void EverbluMeterComponent::setup() {
    // Create adapters specific to ESPHome
    auto config = new ESPHomeConfigProvider();
    auto time = new ESPHomeTimeProvider(this->time_);
    auto publisher = new ESPHomeDataPublisher();
    
    // Configure adapters from YAML settings
    config->setMeterYear(this->meter_year_);
    config->setMeterSerial(this->meter_serial_);
    // ... more configuration ...
    
    // Link sensors to publisher
    publisher->set_volume_sensor(this->volume_sensor_);
    publisher->set_battery_sensor(this->battery_sensor_);
    // ... more sensors ...
    
    // Create meter reader with injected dependencies
    this->meter_reader_ = new MeterReader(config, time, publisher);
    this->meter_reader_->begin();
}
```

This pattern allows the same `MeterReader` code to work in both standalone MQTT and ESPHome environments without modification.

#### Component File Structure

```
ESPHOME/components/everblu_meter/
├── __init__.py              # Python config schema
├── everblu_meter.h          # Component header
├── everblu_meter.cpp        # Component implementation
├── core/                    # Core functionality (from src/)
│   ├── cc1101.h/.cpp       # CC1101 radio driver
│   ├── utils.h/.cpp        # Utility functions
│   └── wifi_serial.h/.cpp  # WiFi serial (unused in ESPHome)
├── services/                # Business logic (from src/)
│   ├── meter_reader.h/.cpp
│   ├── frequency_manager.h/.cpp
│   ├── schedule_manager.h/.cpp
│   ├── meter_history.h/.cpp
│   └── storage_abstraction.h/.cpp
└── adapters/                # Adapter pattern (from src/)
    ├── config_provider.h
    ├── time_provider.h
    ├── data_publisher.h
    └── implementations/
        ├── esphome_config_provider.h
        ├── esphome_time_provider.h/.cpp
        └── esphome_data_publisher.h/.cpp
```

### Extending the Component

To add new features:

1. **Add Configuration**: Update `CONFIG_SCHEMA` in `__init__.py`
2. **Add Setter**: Add corresponding setter in `everblu_meter.h`
3. **Pass to Adapter**: Call setter in `to_code()` function
4. **Use in Core**: Access via adapter interface in `MeterReader`

Example - adding a new parameter:

```python
# In __init__.py
CONFIG_SCHEMA = cv.Schema({
    # ... existing config ...
    cv.Optional("my_new_setting", default=42): cv.int_range(min=0, max=100),
})

# In to_code()
async def to_code(config):
    # ... existing code ...
    cg.add(var.setMyNewSetting(config["my_new_setting"]))
```

```cpp
// In everblu_meter.h
void setMyNewSetting(int value) { my_new_setting_ = value; }

// In everblu_meter.cpp setup()
config_provider->setMyNewSetting(this->my_new_setting_);
```

## Support

For issues and questions:

- **GitHub Issues**: [Report bugs](https://github.com/yourusername/everblu-meters-esp8266-improved/issues)
- **Discussions**: [Ask questions](https://github.com/yourusername/everblu-meters-esp8266-improved/discussions)
- **Documentation**: [Read the docs](../docs/)

## License

This component is licensed under the MIT License. See [LICENSE.md](../LICENSE.md) for details.
